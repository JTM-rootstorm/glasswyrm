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

- `glasswyrmd`: placeholder X11-compatible display server entry point.
- `gwm`: planned or placeholder window manager and policy process.
- `gwcomp`: planned or placeholder compositor, renderer, and display authority process.
- `gwctl`: placeholder runtime control utility.
- `gwinfo`: placeholder diagnostics utility.
- `gwtrace`: placeholder protocol/event tracing utility.
- `gwout`: placeholder output configuration utility.

These commands intentionally report scaffold status only until each process has
its bring-up milestone implemented. They do not yet start a usable
X11-compatible session.

## Project Layout

- `include/glasswyrm/`: public C++ headers for the early skeleton libraries.
- `src/core/`: shared identity, feature options, and common state primitives.
- `src/glasswyrmd/`: X11-compatible server process code.
- `src/gwm/`: window manager and window-policy process code.
- `src/gwcomp/`: compositor, renderer, and display authority process code.
- `src/ipc/`: internal IPC contracts shared by `glasswyrmd`, `gwm`, and `gwcomp`.
- `src/protocol/`: protocol-facing support that will grow into X11 decoding.
- `src/compositor/`: scene and headless compositor scaffolding consumed by `gwcomp`.
- `src/backends/`: platform backend stubs, including headless and DRM/KMS paths.
- `src/input/`: input routing scaffolding.
- `src/render/`: renderer abstractions and software renderer scaffolding.
- `tools/`: developer and runtime command-line tools.
- `tests/`: unit and headless integration tests.
- `docs/`: specification, architecture notes, protocol notes, and decisions.

## Gentoo packaging

Gentoo packaging should keep the source tree coherent while allowing runtime
components to be built, installed, and updated independently once Meson targets
support that split.

The intended package shape is:

```text
x11-base/glasswyrm       # metapackage or session bundle
x11-base/glasswyrmd      # X11-compatible server
x11-wm/gwm               # Glasswyrm window manager and policy process
x11-base/gwcomp          # compositor and display authority
x11-apps/gw-tools        # gwctl, gwinfo, gwtrace, gwout, gwbench
gui-libs/libgwipc        # shared IPC contract library
gui-libs/libgwproto      # protocol helpers, if installed as a shared library
gui-libs/libgwrender     # renderer helpers, if installed as a shared library
```

Split packages are meant to reduce rebuild and install scope, especially for
`gwm` and `gwcomp`. They should not be assumed to prevent source fetching by
themselves. Use a shared release tarball, local distfile cache, or local git
cache when multiple ebuilds consume the same upstream source.

Fresh Gentoo VM testing should use a local overlay under `packaging/gentoo/` and
exercise `emerge`. Shared folders may provide the overlay, distfiles, and binary
packages, but copying built artifacts directly into the VM is not a substitute
for validating the ebuild path.

## Compatibility

Glasswyrm does not currently claim X11 client compatibility. Compatibility will
advance by tested tiers described in `docs/GLASSWYRM_SPEC.md`.
