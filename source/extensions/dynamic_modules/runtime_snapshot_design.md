# Runtime snapshot batch reads for dynamic modules

Source baseline: `envoyproxy/envoy@46b5ab37f081390ea0a404be53eea82ef01ffa72`.
This fork adds the ABI, C++ host implementation, Rust SDK, and Go HTTP SDK described below.

## Purpose

Envoy's existing RTDS subscription publishes the merged runtime snapshot. A dynamic module
queries related values at its decision point with one host callback:

```text
RTDS server -> Envoy RTDS subscription -> merged runtime snapshot
            -> module batch callback -> copied values -> module decision
```

The API reads the merged runtime, including configured layer precedence. It adds no RTDS client,
subscription, notification, runtime writes, or long-lived snapshot handle.

## Implemented surface

All entrypoints share the implementation in [runtime.cc](runtime.cc) and types in [abi.h](abi/abi.h).
Their prefix is `envoy_dynamic_module_callback_`.

| Suffix | Existing host pointer | Execution context |
|---|---|---|
| `http_filter_runtime_read_batch` | HTTP filter | Current request/response worker hook |
| `http_filter_config_runtime_read_batch` | HTTP filter config | Config construction or main-thread config hook |
| `cluster_runtime_read_batch` | Dynamic cluster | Current main-thread cluster hook |
| `cluster_lb_runtime_read_batch` | Dynamic-cluster LB | Current worker LB hook |

The HTTP filter retains its configuration, which retains the server context. The cluster LB
retains its cluster through its existing shared handle. Each adapter reaches that owner's loader.
No process-global loader pointer is introduced.

Standalone dynamic load balancers, cluster specifiers, and other module extension points are
outside this first patch. Their opaque handles need their own adapters. The Rust SDK exposes all
four implemented roles; Go exposes HTTP request/config handles through the optional
`shared.RuntimeReader` interface. Existing Go custom handles need no new methods. The C++ SDK has
no convenience wrapper yet; the C ABI is available.

## Call contract

```c
envoy_dynamic_module_type_runtime_read_result
envoy_dynamic_module_callback_http_filter_runtime_read_batch(
    envoy_dynamic_module_type_http_filter_envoy_ptr context,
    const envoy_dynamic_module_type_runtime_request* requests, size_t requests_size,
    const envoy_dynamic_module_type_runtime_condition* condition,
    envoy_dynamic_module_type_runtime_value* values,
    char* strings, size_t strings_capacity, size_t* strings_size_out);
```

Each request contains a length-delimited key, a kind, and scalar fallback fields. Kinds are
boolean, unsigned integer, double, and raw string. Scalars use Envoy's native typed getters:
missing or invalid values return the supplied fallback. Raw strings distinguish missing from
empty and use offsets/lengths into the caller's output arena. Runtime objects are not decoded
into application policy objects by this API.

Non-boolean reads and revision conditions reject Envoy runtime-feature keys because the native
raw/numeric getters assert against them. Boolean reads use the caller's fallback, which does not
necessarily equal the compiled default for a runtime guard. This API does not perform sampling
or deprecated-feature checks.

| Result | Meaning |
|---|---|
| `Ok` | All result slots and copied bytes form one complete batch |
| `Unchanged` | Optional revision matched; no value outputs are valid |
| `BufferTooSmall` | Required byte capacity is returned; retry the complete batch |
| `LimitExceeded` | A hard input/output bound was exceeded |
| `InvalidArgument` | Invalid descriptor, kind/key combination, or condition |
| `Unavailable` | No snapshot was available |

The bounds are 64 keys, 16 KiB combined input keys/condition bytes, and 4 MiB output bytes.
Duplicate keys count independently. All size arithmetic is bounded before use.
`strings_size_out` reports bytes used on success, required on `BufferTooSmall`, and zero otherwise.
On other results neither result slots nor arena bytes are written. Buffers must not overlap.

## Ownership, consistency, and retries

After validation, the helper acquires `Loader::threadsafeSnapshot()` exactly once. It retains
that shared pointer through the condition, every lookup, output sizing, and copying. A publication
during the call cannot mix old and new values within the batch or invalidate its borrowed strings.
The retained pointer is released before returning. No host-owned value pointer escapes.

A sizing retry is a new batch and may see newer state. Never combine a revision from one attempt
with payload bytes from another. The SDKs expose the required capacity and let callers choose
bounded retry policy. Rust and Go return owned strings/bytes that survive scratch reuse.

Call only from the owning Envoy hook. A background goroutine/thread must not call through a
retained request/config handle. Background verification can consume copied bytes after acquisition
on the owning Envoy execution context. The underlying snapshot references random/statistics
services, so exposing arbitrary retained snapshots would need a separate shutdown contract.

Batch consistency does not make updates to separate RTDS resources transactional or synchronize
adoption across workers or the fleet. Retain copied values if a request needs to reuse a decision.

## Conditional payload reads and Pax

An optional condition holds an application revision key and its last accepted value. If the
current revision exists and matches, the host returns `Unchanged` before reading payload keys.
A missing revision always proceeds with the batch. A present empty revision differs from no
condition. First acquisition should pass no condition.

The application must change its revision for every relevant change or removal, including changes
introduced through higher-precedence runtime layers. The ABI cannot enforce this invariant.
The RTDS transport version is not exposed as a substitute for an application revision.

For Pax, request the revision, signed data, and related policy identity in one batch. Verify and
prepare the copied candidate before publishing it to request-time code. Invalid candidates must
not advance the accepted revision or freshness state. `Unchanged` proves only equality; it does
not authenticate data or renew a freshness lease. RTDS loss retains the last accepted runtime
snapshot; Pax must enforce its own expiration/fail-closed rules.

The host cost is one snapshot acquisition, bounded native key lookups, and copies of requested
strings. Registered Envoy threads use the loader's thread-local snapshot path; other loader
contexts may take a read lock. The host uses stack staging arrays. The Go wrapper allocates C
descriptor/key storage and Go-owned results; the Rust wrapper allocates descriptors/results.
This is an in-memory operation with no request-time network or parsing, but it is not a plain
variable load. No latency benchmark is claimed.

## Compatibility and validation

The additions preserve existing C declarations and give Rust traits default unsupported methods.
Modules using new symbols require an Envoy binary built with this patch. The existing loader's ABI
version mismatch warning is not capability negotiation; an older host cannot provide these symbols.
Build the module SDK and Envoy from the same fork revision.

Tests cover native scalar fallbacks, embedded zero bytes, missing/empty strings, conditions,
capacity/overflow limits, no partial writes, retries against newer snapshots, snapshot retention,
and all four host adapters. Rust tests exercise owned result conversion and error mapping.
The companion `dio/envoy-rtds-runtime-poc` project exercises actual RTDS updates through a Go
HTTP module, including live enable/disable, larger payload retries, concurrent requests, and
retention during an RTDS outage.
