# Glasswyrm

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
modern Linux, focused on clean internals, explicit display policy, HDR, VRR,
and per-output scaling.

Milestone 10 DRM/KMS software scanout is complete and validated on the
configured Gentoo QXL VM through its real DRM primary node, graphical console,
and VT lifecycle.
Milestone 11 implementation adds an opt-in libinput/libxkbcommon input path,
core cursor/grab/selection behavior, interactive GWM bindings, coordinated
input/VT state, and a session launcher. The pinned xterm patch 410 core-font
ASCII profile is validated by the live interaction, restart, VT, normalized
trace, canonical-frame, graphical-console, restoration, and archive gates.
Milestone 12 implementation adds an opt-in game-compatibility registry,
bounded BIG-REQUESTS, MIT-SHM, XFIXES, DAMAGE, RENDER, COMPOSITE, and RANDR
subsets, SDL-oriented EWMH policy, eventfd CPU-buffer readiness, a renderer
abstraction with optional EGL/GLES composition, and damage-aware DRM copies.
The exact SDL 2.32.10 software-X11 profile and official workloads are pinned,
and their narrow external-client claim is accepted by the clean M11-to-M12
Gentoo VM sequence with complete evidence.
Milestone 13 implementation adds an opt-in compositor-authoritative output
model, stable headless and DRM output identities, atomic layout changes,
several logical headless outputs, rational compositor scaling and all output
transforms, one-workspace multi-output policy, GWIPC API 0.8, read-mostly
multi-output RANDR, experimental `GW_SCALE` 0.1, and real `gwinfo`/`gwout`
clients. Software remains canonical and the DRM boundary remains exactly one
physical connector. The bounded M13 profile is accepted by the clean
M12-to-M13 Gentoo VM sequence with validated headless, single-QXL DRM,
VT/input recovery, restoration, cleanup, and archive evidence.
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
policy transaction. Milestone 9 adds a built-in fixed font, bounded core text
and raster requests, depth-1 pixmaps, child-window flattening, coordinate
queries, and opt-in safe protocol tracing. Pinned `xeyes` 1.3.1 and `xclock`
1.2.0 profiles pass exact frame and normalized-trace goldens in the Gentoo VM.
Milestone 10 preserves that canonical software frame and adds an opt-in Linux
DRM/KMS presenter using XRGB8888 dumb buffers, atomic modesetting when fully
usable, a legacy fallback, delayed presentation completion, and direct or
inherited session ownership. Headless remains the default. Milestone 11 builds
the first real-device and interactive baseline on those boundaries. M12 keeps
software rendering as the default and adds only a constrained compositor-side
GLES path; broad X11 application support, the XKB extension, client graphics
APIs, and direct scanout remain absent. M13 adds multi-output policy only for
one workspace and does not claim broad toolkit scaling or physical
multi-connector support.

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

Meson's built-in `werror` option is available for strict builds. The DRM
backend is Linux-only and opt-in; enabling it discovers libdrm and verifies the
KMS, atomic, page-flip, AddFB2, and dumb-buffer API at configure time:

```sh
meson setup build-drm -Ddrm_backend=true -Dwerror=true
meson compile -C build-drm
meson test -C build-drm --print-errorlogs
```

The default `drm_backend=false` build does not discover or link libdrm. The
headless and DRM backends may be built together, or a DRM-only compositor may
be configured with `-Dheadless_backend=false -Ddrm_backend=true`. IPC tracing
applies only to `libgwipc`; the remaining reserved renderer, policy, assembly,
and experimental switches do not enable runtime behavior until their
milestones land.

The real-input backend is likewise Linux-only and opt-in:

```sh
meson setup build-m11 \
  -Dlibinput_backend=true -Ddrm_backend=true -Dwerror=true
meson compile -C build-m11
meson test -C build-m11 --print-errorlogs
```

With `libinput_backend=false` (the default), Meson does not discover or link
libinput or libxkbcommon. Enabling it requires `glasswyrmd` and `libgwipc` plus
libinput, libxkbcommon, xkeyboard-config data, and timerfd support.

The optional M12 compositor renderer is built separately from the DRM
presenter:

```sh
meson setup build-m12 \
  -Dexperimental=true \
  -Drender_gl=true \
  -Ddrm_backend=true \
  -Dlibinput_backend=true \
  -Dwerror=true
meson compile -C build-m12
meson test -C build-m12 --print-errorlogs
```

`render_gl=true` requires `gwcomp`, EGL 1.5 platform entry points, and OpenGL
ES 2.0; GBM is optional. The default `render_gl=false` build does not discover
or link EGL, GLES, or GBM. `experimental=true` builds the M12 server code but
does not enable it at runtime without `--game-compat`.

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
            [--game-compat] [--disable-extension NAME]...
            [--output-model] [--control-socket PATH]
            [--scale-protocol]
            [--x11-trace PATH]
            [--libinput-device PATH]...
            [--xkb-rules NAME] [--xkb-model NAME]
            [--xkb-layout NAME] [--xkb-variant NAME]
            [--xkb-options LIST]
            [--repeat-delay-ms N] [--repeat-rate-hz N]
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

`gwcomp` uses the scalar software scene renderer by default. Milestone 12 adds
an independent renderer-selection boundary and optional deterministic report:

```sh
./build/src/gwcomp --renderer software \
  --renderer-report /tmp/glasswyrm-renderer.jsonl \
  --ipc-socket /tmp/glasswyrm-gwcomp.sock \
  --dump-dir /tmp/glasswyrm-dumps
```

The report path must not already exist. `--renderer auto` honestly records its
selection and fallback reasons. Forced `--renderer gles` fails at startup when
the build has no EGL/GLES renderer; it never silently selects software.

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

Add `--game-compat` only to an integrated software-content launch built with
`experimental=true`:

```sh
./build-m12/src/glasswyrmd --display 99 \
  --wm-socket /tmp/glasswyrm-gwm.sock \
  --compositor-socket /tmp/glasswyrm-gwcomp.sock \
  --software-content \
  --game-compat
```

This selects the fixed M12 setup and extension registry. Without it, extension
discovery remains historically absent. A repeated `--disable-extension NAME`
is valid only with the game profile and is reserved for explicit fallback
testing; names and assignments come from the immutable registry. See the
[Milestone 12 protocol profile](docs/protocols/x11-milestone-12.md).

## Milestone 9 compatibility work

M9 targets only `xeyes` 1.3.1 and `xclock` 1.2.0 with the exact commands and
environment in `tests/compat/m9/clients.toml`. Shape and Render remain absent.
The official release hashes are pinned, and reviewed frame and normalized
trace fixtures prove the analog, digital, xeyes, and combined profiles. This is
a command-specific compatibility claim, not broad xeyes, xclock, Xt, or Xaw
compatibility.

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

The trace path must not already exist. The fresh-VM acceptance route is
intentionally gated on committed source and verified client hashes:

```sh
./tools/gw-vm milestone9-runtime-test --yes
```

See [the compatibility profiles](docs/compatibility/README.md) for the exact
supported subsets and current evidence status.

## Milestone 10 DRM/KMS output

Start `gwm` normally, then launch a DRM-enabled `gwcomp` on a verified spare VT
and primary DRM node:

```sh
./build-drm/src/gwm --ipc-socket /run/glasswyrm/gwm.sock
./build-drm/src/gwcomp \
  --backend drm \
  --ipc-socket /run/glasswyrm/gwcomp.sock \
  --drm-device /dev/dri/card0 \
  --tty /dev/tty2 \
  --connector Virtual-1 \
  --mode 1024x768 \
  --drm-api auto \
  --mirror-dump-dir /var/tmp/glasswyrm-mirror \
  --scene-manifest /var/tmp/glasswyrm-scenes.jsonl \
  --drm-report /var/tmp/glasswyrm-drm.jsonl
./build-drm/src/glasswyrmd --display 99 \
  --wm-socket /run/glasswyrm/gwm.sock \
  --compositor-socket /run/glasswyrm/gwcomp.sock \
  --software-content
```

This path requires a Linux DRM primary node with dumb-buffer and KMS support,
permission to become DRM master, a free Linux VT, and an exact output mode.
The M10 backend presents exactly one unscaled, unrotated XRGB8888 output using
two CPU-mapped dumb buffers. It provides no GPU acceleration, real input,
hotplug recovery, cursor plane, multiple outputs, VRR, or HDR.

An external session owner can instead pass an already usable primary-node FD
with `--drm-fd N --external-session`; `gwcomp` then never acquires/drops DRM
master or changes VT/KD state. See [the output documentation](docs/output/) for
the exact ownership, atomic fallback, reporting, and restoration boundaries.

The fixed hardware acceptance route is:

```sh
./tools/gw-vm milestone10-runtime-test --yes
```

The accepted route starts from the single internal `base` snapshot, proves the
historical M9 gate before libdrm is installed, resets to `base`, then validates
QXL atomic KMS scanout, exact screenshots, VT release/acquire, a later
input-driven repaint, ordered restoration, and the checksum-protected evidence
archive. Mocked host coverage remains an additional regression layer rather
than a substitute for this graphical-console proof.

## Milestone 11 interactive desktop profile

`glasswyrm-session` starts `gwm`, DRM `gwcomp`, real-input `glasswyrmd`, and an
optional initial client without invoking a shell or acquiring privileges. The
caller must already be able to open the selected DRM node, VT, and explicit
input device paths. See [the launcher contract](docs/session/M11_SESSION_LAUNCHER.md)
and [input documentation](docs/input/).

The validated compatibility claim is only xterm patch 410 under the exact
core-font ASCII build, environment, and command pinned in
`tests/compat/m11/clients.toml`: one US pc105 keymap, one workspace, software
cursor, minimum PRIMARY/CLIPBOARD exchange, Alt+Button1 move, Alt+Button3
resize, Alt+F4 close, and one DRM/KMS output. Passive grabs are limited to the
observed `GrabButton` path; `UngrabButton` and passive key grabs are
unsupported. Xft/Unicode, the XKB extension, XIM/compose, arbitrary layouts,
themed/hardware cursors, full grabs, clipboard persistence, decorations, and
multiple workspaces/outputs are unsupported.

The fixed VM command is:

```sh
./tools/gw-vm milestone11-runtime-test --yes
```

That command establishes acceptance only when its live trace, interaction,
VT/restart, canonical-frame, console screenshot, restoration, and archive
checks all pass.

After that command has completed its build phase at the current commit, a late
interactive failure can be investigated without repeating the compiler matrix:

```sh
./tools/gw-vm milestone11-interactive-rerun --yes
```

This development-only rerun requires a guest where the complete M11 build
matrix previously reached the runtime build and the pinned xterm cache is still
valid. It reconfigures and incrementally compiles only that runtime tree against
the current committed source, then repeats the live input, VT/restart,
restoration, and archive path. Its summary explicitly is not an acceptance
result; the complete clean `milestone11-runtime-test` remains the final gate.

## Milestone 12 efficient SDL profile

The M12 target is the unmodified SDL 2.32.10 X11 software-renderer build and
the exact repository probe, official `testdraw2`, and official `testsprite2`
commands pinned in `tests/compat/m12/clients.toml`. The game profile exposes
one output and workspace, a bounded extension set, client TrueColor colormaps,
fullscreen-desktop and borderless policy, and eventfd-synchronized damaged
buffer publication. The scalar renderer remains canonical; forced GLES uses
only the constrained compositor renderer and still hands the same
`SoftwareFrame` to headless or DRM presentation.

The accepted compatibility claim is bounded by this required release order:

```sh
./tools/gw-vm reset --yes
./tools/gw-vm milestone11-runtime-test --yes
./tools/gw-vm reset --yes
./tools/gw-vm milestone12-runtime-test --yes
```

The final command proves both byte orders, MIT-SHM and its fallback,
software/GLES opaque-frame equality, real input/clipboard/cursor/close,
fullscreen geometry restore, damage-aware upload and scanout metrics,
GWM/compositor/VT replay, KMS/KD/VT/getty restoration, cleanup, and evidence
archive integrity. The [SDL profile](docs/compatibility/M12_SDL.md) records the
exact accepted boundary; it is not a general SDL or game compatibility claim.

## Milestone 13 output and scaling profile

The M13 profile is explicit: start `gwcomp` with repeated headless outputs and
start `glasswyrmd` with `--output-model`, a private `--control-socket`, and
optionally `--scale-protocol`. `glasswyrm-session --backend headless` forwards
that topology without DRM, TTY, or input devices. See the
[output model](docs/output/M13_OUTPUT_MODEL.md),
[rendering contract](docs/rendering/M13_SCALE_AND_TRANSFORM.md), and
[session launcher](docs/session/M11_SESSION_LAUNCHER.md).

Acceptance uses the historical gate first and does not treat host tests as a
substitute:

```sh
./tools/gw-vm reset --yes
./tools/gw-vm milestone12-runtime-test --yes
./tools/gw-vm reset --yes
./tools/gw-vm milestone13-runtime-test --yes
```

The accepted sequence proves the two-output headless layout, deterministic
`gwinfo`/`gwout`, RANDR and `GW_SCALE` wire paths, software/GLES equivalence,
restart and accepted reconfiguration, then the fixed one-output QXL scale and
transform path with VT/input recovery, restoration, cleanup, and validated
evidence. This is the bounded profile described below, not a broader toolkit
scaling or physical multi-connector compatibility claim.

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

- `glasswyrmd`: owns X11 protocol, resource, input, cursor, grab, and selection
  truth; implements standalone M2, the integrated lifecycle, M8 synthetic
  input, the opt-in M11 real-input profile, and the opt-in M12 extension/EWMH
  game profile. M13 adds the opt-in dynamic screen, output transaction, RANDR,
  `GW_SCALE`, and same-UID output-control paths.
- `gwm`: owns window-management policy truth; it accepts lifecycle-extended
  complete policy snapshots from `glasswyrmd`, including M13 per-output work
  areas and deterministic output assignment in one workspace.
- `gwcomp`: owns software composition and final display authority; it retains
  the default headless path and can present the identical canonical frame
  through the opt-in M10 DRM/KMS backend. It accepts M6 metadata-only scenes
  without fake buffers and buffered ProtocolServer surfaces in M7
  software-content mode and capability-gated software cursor/session state.
  M12 adds independent software/GLES renderer selection, eventfd readiness,
  and damage-aware DRM copies without moving presentation authority. M13 adds
  stable inventory and atomic native per-output frame sets.
- `glasswyrm-session`: unprivileged three-process and optional-client
  orchestrator for the M11 development session with additive M12 game-profile
  and renderer argument forwarding plus M13 headless/output-model profiles.
- `gwctl`: future runtime control utility.
- `gwinfo`: deterministic M13 output and window diagnostics client.
- `gwtrace`: future protocol/event tracing utility.
- `gwout`: complete-layout M13 output configuration client.
- `gwbench`: future rendering/compositor benchmark utility.

The installed API 0.8 `libgwipc.so.0` C ABI uses nonblocking local
`AF_UNIX`/`SOCK_SEQPACKET`, fixed little-endian wire 1.0 records, same-UID peer
credentials, bounded queues, descriptor passing, snapshots, and compositor,
window-policy, lifecycle, synthetic-input, session-state, interactive-policy,
eventfd CPU-buffer synchronization, and output-management vocabularies. Wire
1.0 and
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
- `src/backends/`: component-neutral software frames, tested headless output,
  DRM/KMS software scanout, and direct/external session boundaries.
- `src/input/`: synthetic/real input routing, libinput conversion, xkb state,
  repeat, and cursor-model primitives.
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

The Milestone 9 pinned-client and Milestone 10 DRM acceptance commands are:

```sh
./tools/gw-vm milestone9-runtime-test --yes
./tools/gw-vm milestone10-runtime-test --yes
```

M10 starts from the accepted M9-clean guest, installs libdrm without Mesa or an
X server, and requires a pre-existing kernel DRM primary node. The configured
QXL guest meets that boundary and the command validates atomic scanout,
graphical-console screenshots, VT switching, input-driven repaint, restoration,
and the required binary evidence archive.

The Milestone 11 through Milestone 13 acceptance commands are:

```sh
./tools/gw-vm milestone11-runtime-test --yes
./tools/gw-vm milestone12-runtime-test --yes
./tools/gw-vm milestone13-runtime-test --yes
```

M12 must be preceded by the documented reset/M11/reset sequence so the
historical dependency-absence and pinned xterm boundary are re-proven before
Mesa, X11 extension libraries, and SDL build dependencies are installed.
M13 must likewise follow the documented reset/M12/reset sequence so the
accepted historical profile is re-proven before output-model validation.

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
The [Milestone 11 profile](docs/protocols/x11-milestone-11.md) records the
accepted interactive subset, while the
[xterm profile](docs/compatibility/M11_XTERM.md) limits the external-client
claim to the pinned patch 410 core-font ASCII invocation.
The [Milestone 12 profile](docs/protocols/x11-milestone-12.md) records the
implemented opt-in extension and efficient-buffer boundary. Its
[SDL 2.32.10 profile](docs/compatibility/M12_SDL.md) records the narrow claim
accepted by the clean Gentoo VM evidence gate.
The [Milestone 13 profile](docs/protocols/x11-milestone-13.md) records the
opt-in output and scale protocol boundary without widening those external
client claims.

The exact Milestone 13 compatibility statement is:

Supported:

- several logical headless outputs
- one physical DRM output
- stable output inventory and capabilities
- integer and fractional compositor scaling
- all output transforms
- legacy client fallback scaling
- repository GW_SCALE v0.1 client
- multi-output RANDR reporting
- gwout/gwinfo control and diagnostics
- one workspace

Unsupported:

- several physical DRM connectors
- hotplug recovery
- physical mode setting through gwout/RANDR
- toolkit GW_SCALE integration
- Xft DPI integration
- output persistence
- VRR/HDR/color management
