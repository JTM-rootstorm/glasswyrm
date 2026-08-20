# GWIPC Rust Transition ABI Disposition

Status: full preservation selected for WP0C
Date: 2026-08-20

## Installed contract

The retained Meson install owns GWIPC API 0.9.0 with SONAME `libgwipc.so.0`,
`gwipc.pc`, `glasswyrm/ipc.h`, `glasswyrm/ipc.hpp`, and the public headers under
`glasswyrm/ipc/`. API 0.9 intentionally preserves the earlier 0.1-through-0.8
public symbol/version contracts.

A workspace-wide search found no consumers in Deepwyrm, Wyrmroot, or
Prismdrake. Glasswyrm itself retains unchanged external-consumer canaries under
`tests/install/`: paired C17/C++20 consumers cover API 0.1 through API 0.9.
The WP0C staged-install gate compiled and executed every one of those consumers
against the installed library and pkg-config metadata.

## Disposition

**Preserve the complete installed GWIPC 0.9 ABI during the Rust transition.**

No symbol, header, layout, symbol-version node, SONAME, or pkg-config contract
may be removed merely because no other OS-Project repository currently imports
it. Installed libraries can have users outside this workspace, and the project
specification already presents GWIPC 0.9 as a stable compatibility surface.

`gw-ipc-capi` remains migration scaffolding, not the installed replacement. It
currently covers only version/status queries and the SurfaceRemove contract
ownership helpers. Its covered wire codec delegates to `gw-wire`/`gw-types`.
Further bridge expansion must continue using those shared implementations.

## Public cutover gate

The Rust implementation may assume the public `libgwipc` names only after all
of the following are true:

1. the accepted installed 0.9 symbol/version map is represented;
2. public structure layout and version/header tests pass unchanged;
3. every staged C17/C++20 consumer from API 0.1 through 0.9 passes unchanged;
4. transport/listener/message ownership and connection semantics are covered;
5. `libgwipc.so.0`, headers, and `gwipc.pc` have one package owner with no
   legacy/Rust collision; and
6. all C ABI codec paths delegate to shared Rust wire/types implementations.

Until then, native Meson `libgwipc` remains the installed owner and the Rust
bridge is installed only under transition-specific names for smoke testing.
