# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

The project is currently at Milestone 0: repository skeleton. The initial goal
is a buildable, testable base for bringing up the core server, protocol parser,
headless compositor, and software renderer without requiring real DRM/KMS
hardware.

## Build

Glasswyrm uses Meson and Ninja.

```sh
meson setup build -Dheadless_backend=true -Drender_software=true
meson compile -C build
meson test -C build
```

For an early sanitizer build:

```sh
meson setup build-asan -Dheadless_backend=true -Drender_software=true -Dasan=true -Dubsan=true
meson test -C build-asan
```

## Current binaries

- `glasswyrmd`: placeholder display server entry point.
- `gwctl`: placeholder runtime control utility.
- `gwinfo`: placeholder diagnostics utility.
- `gwtrace`: placeholder protocol/event tracing utility.
- `gwout`: placeholder output configuration utility.

These commands intentionally report scaffold status only. They do not yet start
an X11-compatible server.

## Project Layout

- `include/glasswyrm/`: public C++ headers for the early skeleton libraries.
- `src/core/`: server identity, feature options, and shared state primitives.
- `src/protocol/`: protocol-facing support that will grow into X11 decoding.
- `src/compositor/`: scene and headless compositor scaffolding.
- `src/backends/`: platform backend stubs.
- `src/input/`: input routing scaffolding.
- `src/render/`: renderer abstractions and software renderer scaffolding.
- `tools/`: developer and runtime command-line tools.
- `tests/`: unit and headless integration tests.
- `docs/`: specification, architecture notes, protocol notes, and decisions.

## Compatibility

Glasswyrm does not currently claim X11 client compatibility. Compatibility will
advance by tested tiers described in `docs/GLASSWYRM_SPEC.md`.
