# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

The project is currently at Milestone 2. `glasswyrmd` implements local X11 11.0
setup plus a bounded headless core request subset for unmapped window records,
atoms, and typed properties. `gwm`, `gwcomp`, and the runtime tools remain
Milestone 0 placeholders. There is no mapping, event delivery, input, IPC
contract, window-management policy, compositor, renderer, or display backend.

## Build

Glasswyrm uses Meson and Ninja.

```sh
meson setup build -Dwerror=true
meson compile -C build
meson test -C build --print-errorlogs
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

`compile_commands.json` is generated in each Meson build directory.

## Running the setup server

`glasswyrmd` runs in the foreground and listens only on a filesystem Unix
socket. The defaults create display `:0` at `/tmp/.X11-unix/X0`:

```sh
./build/src/glasswyrmd
./build/src/glasswyrmd --display 99
./build/src/glasswyrmd --display 99 --socket-dir /tmp/glasswyrm-sockets
```

The complete command line is:

```text
glasswyrmd [--display N] [--socket-dir PATH] [--help] [--version]
```

Milestone 2 accepts exactly X11 protocol 11.0 with zero-length authorization
fields. `MIT-MAGIC-COOKIE-1`, Xauthority, and every other authorization method
are unsupported; supplied authorization data is rejected rather than ignored.
Unauthenticated local setup is a research limitation and is unsafe for a real
multi-user desktop.

After setup, clients may create and recursively destroy unmapped window records,
query geometry and the window tree, intern and name atoms, manipulate typed
window properties, use a synthetic focus reply for synchronization, and send
no-ops. Unsupported core requests return `BadRequest`; recoverable request
errors leave the connection usable.

## Setup probes

The repository-owned raw probe covers both client byte orders:

```sh
./build/tests/x11_setup_probe --display :99 --byte-order little
./build/tests/x11_setup_probe --display :99 --byte-order big
./build/tests/x11_setup_probe --display :99 --malformed
```

When test-only `libxcb` is available, Meson also builds an XCB setup probe:

```sh
DISPLAY=:99 XAUTHORITY=/dev/null ./build/tests/xcb_setup_probe
```

That result means only that a libxcb client can complete connection setup,
inspect the setup record, and disconnect. It does not mean normal XCB or X11
applications work.

## Milestone 2 probes

With `glasswyrmd` running on display `:99`, the fixed raw modes are:

```sh
./build/tests/x11_milestone2_probe --display :99 --byte-order little --basic
./build/tests/x11_milestone2_probe --display :99 --byte-order big --basic
./build/tests/x11_milestone2_probe --display :99 --errors
./build/tests/x11_milestone2_probe --display :99 --cleanup
./build/tests/x11_milestone2_probe --display :99 --cross-endian
```

When test-only `libxcb` is available:

```sh
DISPLAY=:99 XAUTHORITY=/dev/null ./build/tests/xcb_milestone2_probe
```

The libxcb probe creates an unmapped window resource, queries it, manipulates an
atom and format-8 property, destroys the resource, and verifies the resulting
core error. It never maps or displays the window.

## Current binaries

- `glasswyrmd`: owns X11 protocol and resource truth; implements the tested M2
  headless request profile.
- `gwm`: future owner of window-management policy truth.
- `gwcomp`: future owner of composition and final display authority.
- `gwctl`: future runtime control utility.
- `gwinfo`: future diagnostics utility.
- `gwtrace`: future protocol/event tracing utility.
- `gwout`: future output configuration utility.
- `gwbench`: future rendering/compositor benchmark utility.

`gwm`, `gwcomp`, and every runtime tool currently print their Milestone 0
placeholder status and exit. They do not communicate with `glasswyrmd`, create
framebuffers, or access hardware.

## Project Layout

- `include/glasswyrm/`: C++ header namespace; no installed library ABI exists yet.
- `src/scaffold/`: shared placeholder identity and output used only at Milestone 0.
- `src/glasswyrmd/`: X11-compatible server process code.
- `src/gwm/`: window manager and window-policy process code.
- `src/gwcomp/`: compositor, renderer, and display authority process code.
- `src/ipc/`: reserved for versioned internal contracts starting at Milestone 3.
- `src/protocol/`: endian-safe, bounded X11 setup and core wire codecs.
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

Milestone 1 and Milestone 2 source/runtime acceptance are available before the
first ebuilds:

```sh
./tools/gw-vm milestone1-runtime-test --yes
./tools/gw-vm milestone2-runtime-test --yes
```

The fixed M2 scenario synchronizes an owned source tree, runs strict and
sanitizer builds, supervises display `:99` with a transient hardened systemd
unit, runs both M1 setup probes and all raw/libxcb M2 probes, and writes its
reports under `artifacts/vm/latest/`. The separate `full-packaging-test` remains
unavailable until real ebuilds land.

## Compatibility

Glasswyrm claims only the tested behavior documented in
[`docs/protocols/x11-milestone-2.md`](docs/protocols/x11-milestone-2.md). It does
not claim compatibility with normal X11 applications.
