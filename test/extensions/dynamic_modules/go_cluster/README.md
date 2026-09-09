# Go cluster lifecycle fixtures

This standalone consumer module loads one shared library into the unmodified Linux
amd64 Envoy binary for commit `1b6b34b9d6f1b5980acc0e3d32892d920ee98536`.
The test entrypoint checks the exact binary checksum. Go 1.26.7 is used for these
fixtures; the SDK itself retains its existing module toolchain requirement.

```sh
ENVOY_BIN=/path/to/pinned/envoy make test vet
```

Make owns the shared-library build, and tests always run uncached. Ordinary SDK
tests remain free of ABI exports. The cases prove main-thread initialization,
immediate async completion, independent concurrent cancellation, late completion
after a new selection, cancellation racing posting, and worker teardown. Explicit
HTTP control barriers order lifecycle transitions; no sleep stands in for ownership.
These cluster-only tests run with CGO pointer checks enabled.

The fixtures originate in the separate `github.com/dio/upstream` consumer suite,
which additionally validates membership, request preparation, and shared-address
TLS. Keep this module independent of that downstream library.
