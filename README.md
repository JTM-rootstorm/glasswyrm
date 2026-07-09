# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

The project is currently at Milestone 0: repository skeleton. This milestone
establishes only the build, target layout, process names, and stub test harness.
There is no X11 handshake, IPC contract, window-management policy, compositor,
renderer, input path, or display backend yet.

## Build

Glasswyrm uses Meson and Ninja.

```sh
meson setup build
meson compile -C build
meson test -C build
```

For an early sanitizer build:

```sh
meson setup build-asan -Dasan=true -Dubsan=true
meson compile -C build-asan
meson test -C build-asan
```

The three runtime processes and the tool bundle can be selected independently:

```sh
meson setup build-gwm \
  -Dglasswyrmd=false \
  -Dgwm=true \
  -Dgwcomp=false \
  -Dtools=false
meson compile -C build-gwm
```

Meson's built-in `werror` option is available for strict builds. The spec's
backend, renderer, IPC tracing, built-in policy, assembly, and experimental
switches are accepted as reserved configuration at Milestone 0. They do not
enable runtime behavior until their implementation milestones land.

## Current binaries

- `glasswyrmd`: future owner of X11 protocol truth.
- `gwm`: future owner of window-management policy truth.
- `gwcomp`: future owner of composition and final display authority.
- `gwctl`: future runtime control utility.
- `gwinfo`: future diagnostics utility.
- `gwtrace`: future protocol/event tracing utility.
- `gwout`: future output configuration utility.
- `gwbench`: future rendering/compositor benchmark utility.

Every command currently prints its Milestone 0 placeholder status and exits.
The runtime placeholders do not communicate with one another, open sockets,
accept clients, create framebuffers, or access hardware.

## Project Layout

- `include/glasswyrm/`: C++ header namespace; no installed library ABI exists yet.
- `src/scaffold/`: shared placeholder identity and output used only at Milestone 0.
- `src/glasswyrmd/`: X11-compatible server process code.
- `src/gwm/`: window manager and window-policy process code.
- `src/gwcomp/`: compositor, renderer, and display authority process code.
- `src/ipc/`: reserved for versioned internal contracts starting at Milestone 3.
- `src/protocol/`: reserved for X11 decoding beginning at Milestone 1.
- `src/compositor/`: reserved for `gwcomp` scene and composition code.
- `src/backends/`: reserved for headless, DRM/KMS, and possible nested backends.
- `src/input/`: reserved for `glasswyrmd` input routing.
- `src/render/`: reserved for renderer implementations owned by `gwcomp`.
- `tools/`: developer and runtime command-line tools.
- `tests/`: unit and headless integration tests.
- `docs/`: specification, architecture notes, protocol notes, and decisions.

## Gentoo packaging

Gentoo packaging should keep the source tree coherent while allowing runtime
components to be built, installed, and updated independently. Milestone 0
provides narrow Meson switches for the three runtime placeholders and tools;
the overlay and ebuilds remain future packaging work.

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
