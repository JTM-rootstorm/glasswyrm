# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

The project has completed Milestone 8 and Milestone 9 is in progress.
`glasswyrmd` retains its standalone
Milestone 2 mode and can also connect explicitly to `gwm` and `gwcomp` for a
headless top-level lifecycle. The accepted M6 metadata-only mode remains the
default; the explicit M7 `--software-content` mode adds depth-24 pixmaps,
graphics contexts, core software raster requests, exposure events, and
buffered top-level window publication. The explicit M8 synthetic-input option
adds deterministic pointer, button, raw-keycode, crossing, focus, and event
routing through a public GWIPC DiagnosticTool connection.
Milestone 4 has added tested scene/damage, read-only buffer import,
software rendering, bounded headless output, and deterministic PPM dumps.
`gwcomp` accepts and presents every repository-owned synthetic producer
scenario with correlated acknowledgements, buffer releases, reconnect proofs,
and exact-pixel golden coverage. `gwm` is now a separate synthetic policy
service with deterministic placement, stacking, focus, visibility, state, and
policy-snapshot behavior. Integrated startup, full policy snapshots,
metadata-only compositor scenes, deferred X11 lifecycle barriers, and
structural event routing are implemented and remain covered by the M6
regression path. Milestone 7 adds the first honestly painted client windows
without moving X11 raster semantics into `gwcomp`. Milestone 8 keeps input
state and X11 event semantics in `glasswyrmd` while click focus remains a GWM
policy transaction. M9 foundations now include a built-in fixed font, bounded
core text and raster requests, depth-1 pixmaps, child-window flattening,
coordinate queries, and opt-in safe protocol tracing. Pinned xeyes/xclock VM
acceptance evidence is not complete, so these foundations do not yet establish
an external-application compatibility tier. There is still no real-device
input, grabs, XKB, cursors, broad X11 application support, or DRM/KMS output.
Runtime tools remain placeholders.

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
glasswyrmd [--display N] [--socket-dir PATH]
            [--wm-socket PATH --compositor-socket PATH]
            [--software-content] [--synthetic-input-socket PATH]
            [--x11-trace PATH]
            [--help] [--version]
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

For the explicit integrated path, start the listeners before the server:

```sh
./build/src/gwm --ipc-socket /tmp/glasswyrm-gwm.sock
./build/src/gwcomp --ipc-socket /tmp/glasswyrm-gwcomp.sock \
  --dump-dir /tmp/glasswyrm-dumps \
  --scene-manifest /tmp/glasswyrm-scenes.jsonl
./build/src/glasswyrmd --display 99 \
  --wm-socket /tmp/glasswyrm-gwm.sock \
  --compositor-socket /tmp/glasswyrm-gwcomp.sock
```

`glasswyrmd` does not create `/tmp/.X11-unix/X99` until both peer bootstraps
are accepted. Mapped windows on this path have policy and scene metadata but no
client content. Repository-owned raw, XCB, and restart-hold probes exercise the
integrated path without claiming normal application compatibility.

Add `--software-content` to enable the Milestone 7 buffered reference path:

```sh
./build/src/glasswyrmd --display 99 \
  --wm-socket /tmp/glasswyrm-gwm.sock \
  --compositor-socket /tmp/glasswyrm-gwcomp.sock \
  --software-content
```

This opt-in profile supports the documented depth-24 drawable and GXcopy
subset only. Add the M8 listener to the same three-process launch with:

```sh
./build/src/glasswyrmd --display 99 \
  --wm-socket /tmp/glasswyrm-gwm.sock \
  --compositor-socket /tmp/glasswyrm-gwcomp.sock \
  --software-content \
  --synthetic-input-socket /tmp/glasswyrm-input.sock
./build/tests/gwinput_m8 --socket /tmp/glasswyrm-input.sock \
  --scenario click-focus --output /tmp/glasswyrm-input.json
```

The provider supplies device-like records, never target XIDs. This remains a
repository-owned toy-client path and does not claim real input or normal
application compatibility.

## Milestone 9 compatibility work

M9 targets only `xeyes` 1.3.1 and `xclock` 1.2.0 with the exact commands and
environment in `tests/compat/m9/clients.toml`. Shape and Render remain absent.
The protocol implementation and test harness foundations are present, and the
official release hashes are pinned. Reviewed VM traces/frames are still
pending. Do not
interpret this as “xeyes works” or “xclock works” without that evidence.

For a trace-enabled integrated development launch, add a new output path:

```sh
./build/src/glasswyrmd --display 99 \
  --wm-socket /tmp/glasswyrm-gwm.sock \
  --compositor-socket /tmp/glasswyrm-gwcomp.sock \
  --software-content \
  --synthetic-input-socket /tmp/glasswyrm-input.sock \
  --x11-trace /tmp/glasswyrm-m9.jsonl
tests/compat/m9/m9_trace_summarize /tmp/glasswyrm-m9.jsonl
```

The trace path must not already exist. The eventual fresh-VM acceptance route
is intentionally gated on committed source and verified client hashes:

```sh
./tools/gw-vm milestone9-runtime-test --yes
```

See [the compatibility profiles](docs/compatibility/README.md) for the exact
supported subsets and current evidence status.

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

- `glasswyrmd`: owns X11 protocol, resource, and input-routing truth; implements
  standalone M2, the integrated lifecycle, and opt-in M7/M8 content and input.
- `gwm`: owns window-management policy truth; it accepts lifecycle-extended
  complete policy snapshots from `glasswyrmd`.
- `gwcomp`: owns headless composition and final display authority; it retains
  the M4 raster path, accepts M6 metadata-only scenes without fake buffers, and
  accepts buffered ProtocolServer surfaces in M7 software-content mode.
- `gwctl`: future runtime control utility.
- `gwinfo`: future diagnostics utility.
- `gwtrace`: future protocol/event tracing utility.
- `gwout`: future output configuration utility.
- `gwbench`: future rendering/compositor benchmark utility.

The installed API 0.5 `libgwipc.so.0` C ABI uses nonblocking local
`AF_UNIX`/`SOCK_SEQPACKET`, fixed little-endian wire 1.0 records, same-UID peer
credentials, bounded queues, descriptor passing, snapshots, and compositor,
window-policy, and lifecycle vocabularies. Wire 1.0 and
SOVERSION 0 remain unchanged; typed public snapshot controls replace manual
control-byte handling. See
[`docs/ipc/`](docs/ipc/) for its exact API and compatibility boundary.

Runtime tools still print their Milestone 0 placeholder status and exit. The
three runtime processes communicate only in explicit integrated mode; there is
no direct `gwm` to `gwcomp` socket. See the
[M6 topology](docs/architecture/M6_RUNTIME_TOPOLOGY.md) and
[X11 profile](docs/protocols/x11-milestone-6.md) for the implemented boundary
and tested compatibility boundary.

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
./tools/gw-vm milestone5-runtime-test --yes
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

The fixed M5 harness runs strict, sanitizer, GWM-only, and IPC-only builds,
staged legacy/API 0.3 consumers, every synthetic policy scenario, exact JSON
hash validation, malformed-peer isolation, and a hardened transient
`gwm-m5.service`. It collects `milestone5-policies.tar` and strict summary
evidence. The terminal-only Gentoo acceptance run passes without Xorg,
Xwayland, DRM, or input devices.

The fixed Gentoo acceptance command is:

```sh
./tools/gw-vm milestone6-runtime-test --yes
```

It runs the component build matrix, sanitizer suite, raw and XCB probes, live
peer-restart hold probe, fixture checks, and artifact validation with Xorg and
Xwayland absent.

The Milestone 7 rendering acceptance command is:

```sh
./tools/gw-vm milestone7-runtime-test --yes
```

It preserves the M6 metadata/no-PPM substage, then validates both client byte
orders, drawable resources, exposure events, deterministic buffered frames,
buffer replacement and release, and live compositor/GWM restart replay.

The Milestone 8 synthetic-input acceptance command is:

```sh
./tools/gw-vm milestone8-runtime-test --yes
```

It preserves M1-M7 regressions, runs both raw client byte orders and the
two-client XCB script, validates event and framebuffer goldens, isolates a
malformed provider, and holds the same X11 and input connections across GWM
and compositor restart. No Xorg, Xwayland, libinput, or device access is used.

## Compatibility

Glasswyrm claims the standalone behavior documented in
[`docs/protocols/x11-milestone-2.md`](docs/protocols/x11-milestone-2.md). The
[Milestone 6 profile](docs/protocols/x11-milestone-6.md) records the narrower
integrated behavior accepted for M6. The
[Milestone 7 profile](docs/protocols/x11-milestone-7.md) records the exact
opt-in drawable and raster subset. The
[Milestone 8 profile](docs/protocols/x11-milestone-8.md) records the exact
synthetic event-routing subset. None is a compatibility claim for normal X11
applications.
