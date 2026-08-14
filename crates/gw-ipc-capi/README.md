# gw-ipc-capi

`gw-ipc-capi` is temporary migration scaffolding. It currently covers only:

- API and maximum wire version queries;
- status strings; and
- the pure `SurfaceRemove` contract encode/decode path and its opaque payload
  and decoded-contract ownership helpers.

It does not implement transport, connection, listener, message ownership, or
the rest of API 0.9. It is not installed and must not replace native
`libgwipc` yet.

Rust's `cdylib` link already uses an internal export-control script, so adding
the legacy GNU version graph to that link is toolchain-fragile. The Rust
library therefore exports internal `gwipc_rust_*` entry points. The bounded C
shim in `native/gwipc_capi_shim.c` forwards the covered public ABI and the
test link applies `native/gwipc_capi.map`. This keeps the versioned surface
explicit without adding an offline Cargo build dependency or changing the
native install target.

From the repository root, run the isolated smoke gate with:

```sh
crates/gw-ipc-capi/tests/run_smoke.sh "$PWD" \
  "$PWD/target/gw-ipc-capi-smoke" /tmp/gw-ipc-capi-smoke
```

The gate builds the Cargo `cdylib` and `staticlib`, links a test-only versioned
shared object, checks the `GWIPC_0.1` and `GWIPC_0.2` symbol assignments, and
runs a C17 consumer against the shared object and a C++20 consumer against the
static archive using the unchanged public headers.
`native/gwipc_capi_test_message.c` is a smoke-only stand-in for the opaque
message accessors; it is hidden by the version script and is not part of the
bridge surface.
