Added the ``envoy_dynamic_module_callback_cluster_add_hosts_with_hostnames`` ABI callback so
dynamic-module clusters can assign logical hostnames independently of socket addresses. Upstream
features such as automatic SNI and SAN validation can consume the logical hostname. Null or empty
hostnames preserve the legacy synthesized hostname behavior. A non-null hostname array creates an
independent host for each input, allowing logical hosts to share a socket address across incremental
updates; a null array retains legacy address deduplication. The Rust SDK exposes convenience and
priority/locality-aware methods for the new callback.
