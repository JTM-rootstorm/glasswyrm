# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

The project has completed Milestone 4. `glasswyrmd` retains its
tested Milestone 2 local X11 setup and bounded headless core request behavior,
and `libgwipc` provides the tested Milestone 3 versioned local IPC foundation.
Milestone 4 has added tested scene/damage, read-only buffer import,
software rendering, bounded headless output, and deterministic PPM dumps.
`gwcomp` accepts and presents every repository-owned synthetic producer
scenario with correlated acknowledgements, buffer releases, reconnect proofs,
and exact-pixel golden coverage. `gwm` and the runtime tools remain
placeholders; there is still no X11 mapping, event delivery, input, WM policy,
three-process lifecycle, or DRM/KMS output.

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

The three runtime processes, IPC library, and tool bundle can be selected
independently:

```sh
meson setup build-gwm \
  -Dglasswyrmd=false \
  -Dgwm=true \
  -Dgwcomp=false \
  -Dtools=false
meson compile -C build-gwm
```

An IPC-only build excludes every runtime process and tool:

```sh
meson setup build-ipc-only \
  -Dlibgwipc=true \
  -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false
meson compile -C build-ipc-only
meson test -C build-ipc-only --print-errorlogs
```

Meson's built-in `werror` option is available for strict builds. The spec's
backend, renderer, built-in policy, assembly, and experimental switches are
accepted as reserved configuration. IPC tracing applies only to `libgwipc`;
the other switches do not enable runtime behavior until their milestones land.

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
- `gwcomp`: owns the emerging headless composition and final display authority;
  its basic synthetic accepted-frame path is live and golden-tested.
- `gwctl`: future runtime control utility.
- `gwinfo`: future diagnostics utility.
- `gwtrace`: future protocol/event tracing utility.
- `gwout`: future output configuration utility.
- `gwbench`: future rendering/compositor benchmark utility.

The installed `libgwipc.so.0` C ABI uses nonblocking local
`AF_UNIX`/`SOCK_SEQPACKET`, fixed little-endian wire 1.0 records, same-UID peer
credentials, bounded queues, descriptor passing, snapshots, and the first
output/surface/buffer/damage/frame contract vocabulary. See
[`docs/ipc/`](docs/ipc/) for its exact API and compatibility boundary.

`gwm` and every runtime tool still print their Milestone 0 placeholder status
and exit. `gwcomp` runs a GWIPC listener, negotiates a `TestProducer`, and
renders its validated shared-memory buffers headlessly, but does not communicate
with `glasswyrmd` or `gwm` or access display hardware. See [`docs/compositor/`](docs/compositor/) for its
implemented boundary and M4 formats.

## Project Layout

- `include/glasswyrm/ipc*`: installed opaque C ABI and thin C++ RAII wrappers.
- `src/scaffold/`: shared placeholder identity and output used only at Milestone 0.
- `src/glasswyrmd/`: X11-compatible server process code.
- `src/gwm/`: window manager and window-policy process code.
- `src/gwcomp/`: compositor, renderer, and display authority process code.
- `src/ipc/`: versioned wire codecs and local seqpacket transport.
- `src/protocol/`: endian-safe, bounded X11 setup and core wire codecs.
- `src/compositor/`: bounded staged scene, geometry, and damage primitives.
- `src/backends/`: tested headless output/dump primitives plus reserved DRM/KMS
  and possible nested backends.
- `src/input/`: reserved for `glasswyrmd` input routing.
- `src/render/`: reference software renderer plus reserved accelerated paths.
- `tools/`: developer and runtime command-line tools.
- `tests/`: unit and headless integration tests.
- `docs/`: specification, architecture notes, protocol notes, and decisions.

## Gentoo packaging

Gentoo packaging should keep the source tree coherent while allowing runtime
components to be built, installed, and updated independently. Milestone 0
provides narrow Meson switches for the three runtime processes and tools;
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
./tools/gw-vm milestone3-runtime-test --yes
./tools/gw-vm milestone4-runtime-test --yes
```

The fixed M3 scenario additionally runs an IPC-only build, staged install and
C/C++ consumers, then supervises the process probes through a transient
hardened systemd service. Reports are written under `artifacts/vm/latest/`.
The separate `full-packaging-test` remains unavailable until real ebuilds land.

The fixed M4 scenario runs strict, sanitizer, compositor-only, and IPC-only
builds before supervising `gwcomp` as `gwcomp-m4.service`. It drives only the
repository-owned synthetic producer, validates deterministic frame hashes, and
collects the PPMs, frame manifest, and SHA-256 manifest in the binary-safe
`milestone4-frames.tar` artifact. The terminal-only guest must still have Xorg
and Xwayland absent; M4 does not require DRM, Mesa, libinput, or image libraries.

## Compatibility

Glasswyrm claims only the tested behavior documented in
[`docs/protocols/x11-milestone-2.md`](docs/protocols/x11-milestone-2.md). It does
not claim compatibility with normal X11 applications.
