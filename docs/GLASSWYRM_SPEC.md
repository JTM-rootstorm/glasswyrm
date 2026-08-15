# Glasswyrm Project Specification

Status: Living project specification
Repository source of truth: https://github.com/JTM-rootstorm/glasswyrm
Primary target: Gentoo Linux on x86_64
Project tag / code prefix: `gw`

## 1. Project identity

Glasswyrm is a from-scratch, local-first, X11-compatible display stack for modern Linux. It is not a fork of Xorg, XLibre, Xwayland, wlroots, Weston, Mutter, KWin, or any other existing display server/compositor stack.

Glasswyrm should speak enough X11 to run useful local desktop applications while keeping its internal design clean, modern, testable, and intentionally free of unnecessary historical baggage.

The short form `gw` is the preferred internal prefix for code, tools, libraries, extensions, tests, and package naming.

Recommended public description:

> Glasswyrm is a from-scratch, local-first X11-compatible display stack for modern Linux, focused on clean internals, explicit display policy, HDR, VRR, and per-output scaling.

## 2. Core philosophy

Glasswyrm should be treated as a research-grade, fun-first systems project that may become serious if the design proves itself. The project should favor clarity, observability, and incremental proof over premature completeness.

The project should not attempt to recreate Xorg feature-for-feature. The working doctrine is:

> Glasswyrm is X11-compatible where useful, not Xorg-compatible by default.

The stack should initially target modern local desktop use only. It should not preserve legacy behavior merely because old X servers did.

### 2.1 Guiding principles

- Local-first desktop stack.
- Modern Linux first.
- Gentoo first.
- Usable on both systemd and OpenRC systems.
- x86_64 first.
- Rust-first runtime with bounded C, migration-only C++, and selective x86_64 assembly.
- Clean internal compositor-centric architecture.
- X11 protocol compatibility as an external interface, not as an internal design prison.
- Modern display behavior designed in from the start.
- Explicit support tiers instead of vague compatibility promises.
- Extensive headless testing before touching real DRM/KMS hardware.
- Frequent small commits during implementation.
- Push in bulk once a task is complete.

## 3. Explicit non-goals

Initial non-goals:

- Reimplementing all of Xorg.
- Supporting all X extensions.
- Supporting remote TCP X11.
- Supporting old GPU driver models.
- Supporting non-Linux platforms.
- Supporting non-x86_64 architectures before the core design stabilizes.
- Supporting indirect GLX early.
- Supporting ancient server-side font behavior perfectly.
- Supporting Xinerama.
- Supporting full multi-seat behavior early.
- Supporting Wayland as a required runtime dependency.
- Providing production-grade security promises during the early research stage.
- Replacing Xorg or Wayland for general users in the early project life.

Optional future work can revisit these only after the local-first stack is useful.

## 4. Language and implementation choices

Glasswyrm is Rust-first. Rust is the default implementation language for new
runtime code and for migrated behavior. C is a bounded platform/ABI tool, C++
is migration-only legacy code, and x86_64 assembly is reserved for measured
optional hot paths.

### 4.1 Rust

Use Rust for:

- server state, resources, windows, and event routing;
- X11 parsing, dispatch, extension state, and supported protocol behavior;
- GWIPC codecs, contracts, transports, snapshots, and replay;
- window-manager policy and process orchestration;
- compositor scene, output, scaling, VRR, HDR, and color policy;
- input and renderer orchestration;
- configuration, command-line tools, and process supervision; and
- migration and runtime test infrastructure.

Prefer explicit ownership, typed IDs, enums, small state machines,
`Result`-based error propagation, deterministic pure policy functions, narrow
unsafe boundaries, and dependency-light crates. Pure policy and wire crates
must forbid unsafe code. Platform and FFI crates must use explicit unsafe
blocks and document their safety invariants.

### 4.2 C

Use C only when a bounded platform or ABI shim is simpler and more auditable
than expressing the boundary directly in Rust. Candidate uses include selected
DRM/KMS, GBM/EGL/GLES, libinput, udev, generated system glue, and temporary C
ABI exports used by legacy code.

C shims expose small opaque handles and fixed-width plain data. Rust owns
high-level lifecycle and policy. Recommended C dialect remains C17, with C23
only after Gentoo toolchain support is proven.

### 4.3 C++

C++ is permitted only for:

- the existing implementation until its Rust replacement passes the defined
  compatibility gate;
- temporary adapters used to compare legacy and Rust implementations; and
- a dependency with no practical C or Rust boundary.

Do not add new architecture-heavy production C++. Preserve narrow interfaces
and explicit ownership while migrating, but do not broadly refactor legacy
code simply to make the soon-to-be-retired implementation more elegant.

### 4.4 x86_64 assembly

Assembly is allowed only where profiling demonstrates a meaningful hot path
and the optimization is isolated and testable.

Use assembly for:

- Software compositor hot paths after a Rust or C reference implementation exists.
- Pixel blending.
- Pixel format conversion.
- Scaling blits.
- Color conversion experiments.
- Carefully isolated ABI experiments.
- Optional optimized copy routines after profiling.

Assembly rules:

- No assembly-only feature may exist without a correct Rust or C fallback.
- No assembly path may be accepted without golden tests comparing it to the reference implementation.
- Assembly files should use preprocessed `.S` when build-time feature gating is needed.
- Assembly must not be used for high-level policy, window lifetime, input routing, KMS state, selections, or protocol semantics.
- Runtime CPU feature detection must gate AVX2, AVX-512, or other optional paths.
- Optimized code must be easy to disable at build time.

## 5. Dependency policy

Glasswyrm is from scratch at the display-server/compositor layer. It is not from scratch at the kernel/userspace boundary.

Allowed and recommended dependencies:

- Linux kernel DRM/KMS APIs.
- `libdrm`.
- `libinput`.
- `libudev` or equivalent udev access.
- Mesa components where appropriate.
- GBM/EGL for graphics buffer management and rendering experiments.
- Vulkan later, if useful.
- `xcb-proto` XML as a protocol reference/code-generation source, subject to license preservation.
- The Rust standard library and narrowly justified Rust crates.
- Standard C/C++ libraries required by retained native code.
- Common test libraries when justified.

Avoid or prohibit initially:

- Forking Xorg server code.
- Forking XLibre server code.
- Depending on wlroots.
- Depending on Weston, Mutter, KWin, or other compositor internals.
- Depending on Wayland protocols for core runtime behavior.
- Large third-party frameworks that obscure the display-stack internals.

## 6. Build system strategy

Cargo is the primary Rust build and test interface. Meson + Ninja remain in
place during the transition for the legacy C/C++ graph and retained native
shims. These are independent graphs: ordinary targeted Cargo commands must not
invoke a whole Meson build, and ordinary Meson configuration must not rebuild
every Rust test target. A deliberate orchestration command may combine them
for subsystem checkpoints and acceptance gates.

Representative Rust inner-loop commands are:

```sh
cargo check -p gw-ipc
cargo test -p gw-ipc
cargo test -p gwm-core
cargo test -p gwcomp-core
cargo xtask test unit
cargo xtask test contract
cargo xtask test headless gwcomp
```

The exact orchestration spelling may evolve; the separation of compile, unit,
contract, headless, software-acceptance, and guarded hardware tiers may not.

The legacy/native Meson build continues to provide options similar to:

```meson
-Dbackend_headless=true
-Dbackend_drm=true
-Dgwm=true
-Dgwcomp=true
-Dbuiltin_wm_policy=false
-Dsingle_process_debug=false
-Dipc_trace=false
-Drender_software=true
-Drender_gl=false
-Drender_vulkan=false
-Dasm=auto
-Dasan=false
-Dubsan=false
-Dtsan=false
-Dwerror=false
-Dexperimental=true
```

Retained native code should support both GCC and Clang where practical. Local
acceptance should include at least one strict native configuration, one
applicable sanitizer configuration, and Rust formatting/linting/testing.

Do not combine initial Cargo bootstrap with total Meson removal. After all
production C++ is retired, prefer Cargo as the authoritative project build and
retain Meson only if the remaining C/assembly/platform surface or Gentoo
packaging derives clear value from it. That final arrangement is a separate,
bisectable decision.

Split-process options must not blur authority boundaries. A built-in WM policy
mode is acceptable only as an explicitly labeled development/test path; it must
not make `gwcomp` own X11 protocol semantics. A single-process debug build may
host multiple components in one process for tests, but it should still exercise
the same `libgwipc` message contracts used by the real split.

## 7. Repository layout

Recommended transition repository layout:

```text
/
  AGENTS.md
  README.md
  Cargo.toml
  rust-toolchain.toml
  meson.build
  meson_options.txt
  .cargo/
  crates/
    gw-types/
    gw-wire/
    gw-ipc/
    gw-ipc-capi/
    gw-sys/
    gw-platform-linux/
    gw-test-support/
    gwm-core/
    gwm/
    gwcomp-core/
    gwcomp/
    glasswyrm-x11/
    glasswyrm-core/
    glasswyrmd/
    gw-tools/
    xtask/
  docs/
    GLASSWYRM_SPEC.md
    architecture/
    protocols/
    decisions/
  include/
    glasswyrm/
  protocols/
    x11/
    gw/
  src/
    glasswyrmd/
    gwm/
    gwcomp/
    core/
    ipc/
    protocol/
    compositor/
    backends/
      headless/
      drm/
      nested/
    input/
    render/
      software/
      gl/
      vulkan/
    extensions/
    platform/
  tools/
    gwctl/
    gwinfo/
    gwtrace/
    gwout/
    gwbench/
  tests/
    unit/
      wm/
      ipc/
    integration/
    protocol/
    pixel/
    fixtures/
  scripts/
    dev/
    gentoo/
  packaging/
    gentoo/
```

Add the Rust workspace beside the legacy source so migration diffs remain
reviewable. Do not move the C++ tree merely to imitate the crate layout.
`src/compositor/`, `src/backends/`, `src/render/`, and `src/ipc/` remain the
legacy oracle until their replacement gates pass. Temporary `gw-ipc-capi`
exists only to cross the migration boundary. `gw-sys` is the unsafe quarantine
zone; `gwm-core` and `gwcomp-core` must remain testable without hardware.

## 8. Component names

Recommended names:

| Component | Purpose |
|---|---|
| `glasswyrmd` | Full daemon name for the display server. |
| `gwm` | Glasswyrm window manager and window-policy process. |
| `gwcomp` | Glasswyrm compositor, renderer, and display authority process. |
| `gwd` | Short daemon alias if desired. |
| `gwctl` | Runtime control utility. |
| `gwinfo` | Diagnostics and capability report tool. |
| `gwtrace` | Protocol/event tracing utility. |
| `gwout` | Output/display configuration utility. |
| `gwbench` | Rendering/compositor benchmarks. |
| `libgwcore` | Core server utilities and platform wrappers. |
| `libgwproto` | Protocol encoding/decoding helpers. |
| `libgwrender` | Renderer abstractions and software paths. |
| `libgwipc` | Internal IPC contracts shared by `glasswyrmd`, `gwm`, and `gwcomp`. |

Experimental X11 extension names:

- `GW_HDR`
- `GW_SCALE`
- `GW_VRR`
- `GW_COLOR`
- `GW_OUTPUT`
- `GW_PRESENT`

## 9. Compatibility model

Glasswyrm should use tiered compatibility goals. Each tier should be tested before advancing.

| Tier | Target | Expected status |
|---|---|---|
| 0 | Custom toy clients | Required first. |
| 1 | Core X11 handshake and simple XCB clients | Required early. |
| 2 | `xeyes`, `xclock`, similar simple apps | Required before real backend focus. |
| 3 | `xterm` and basic window manager behavior | Required before toolkit work. |
| 4 | SDL/simple games/fullscreen experiments | Required before VRR policy is meaningful. |
| 5 | GTK/Qt apps | Medium-term target. |
| 6 | Browser/Electron/Wine/Proton | Long-term boss fight. |
| 7 | Full daily-driver desktop | Aspirational. |

The project should document exactly which clients and features work. Do not claim broad X11 compatibility without tests.

## 10. Architecture overview

Glasswyrm should use a traditional X11-shaped process split for policy boundaries: an X11-compatible server, a window manager, and a compositor. The split is traditional in shape, but modern in display authority.

The goal is to keep X11 protocol compatibility, window-management policy, and final display presentation independently testable without recreating the historical failure mode where the server owns too much display truth and the compositor is only an X client assembling redirected pixmaps.

External model:

```text
X11 clients
  -> libX11 / XCB / toolkit
  -> glasswyrmd, the Glasswyrm X11-compatible protocol server
  -> Glasswyrm internal IPC

gwm, the Glasswyrm window manager
  -> focus, placement, stacking, workspaces, decorations
  -> ICCCM/EWMH-style policy decisions
  -> policy hints back to glasswyrmd and gwcomp

gwcomp, the Glasswyrm compositor and display authority
  -> surface import, scene graph, renderer, presentation timing
  -> DRM/KMS backend
  -> displays

input devices
  -> libinput / platform backend
  -> glasswyrmd input routing
  -> X11 client events and WM policy events
```

Internal process model:

```text
glasswyrmd
  core server loop
  client connection manager
  X11 protocol decoder/dispatcher
  X11 resource table
  window/surface protocol model
  input router
  legacy compatibility policy
  server side of internal IPC

gwm
  internal IPC client/server role
  window management policy
  focus, raise/lower, placement, and workspace state
  decoration policy
  fullscreen, maximize, and override-redirect decisions
  ICCCM/EWMH compatibility policy

gwcomp
  compositor side of internal IPC
  surface state importer
  WM policy/state consumer
  compositor scene graph
  output manager
  frame scheduler
  render backend
  DRM/KMS backend
  HDR/color/VRR/scaling policy
```

`glasswyrmd` should own X11 protocol semantics, client/resource lifetime, compatibility behavior, selections, atoms, window IDs, raw window state, and input event delivery.

`gwm` should own window-management policy: focus, stacking, placement, workspaces, decorations, reparenting or frame-window behavior if used, fullscreen/maximize interpretation, and ICCCM/EWMH-style decisions. It may produce policy hints that affect presentation, such as fullscreen or direct-scanout eligibility, but it must not own rendering, KMS state, HDR transforms, color interpretation, VRR, or final presentation timing.

`gwcomp` should own final display authority: final composition, frame scheduling, scanout decisions, output configuration, HDR/color transforms, VRR policy, per-output scaling, presentation timing, and DRM/KMS state.

Milestone 10 implements the first narrow part of that display authority. One
component-neutral XRGB8888 `SoftwareFrame` remains the canonical renderer
output. The default headless presenter completes synchronously; an opt-in Linux
DRM presenter copies the same frame into two linear dumb buffers and completes
the compositor transaction only after the blocking first modeset or matching
asynchronous page-flip event. This is a private `gwcomp` boundary and does not
change GWIPC API 0.5.0, SOVERSION 0, or wire 1.0.

The implemented DRM profile is intentionally one connected connector, one
compatible CRTC, one exact-size mode, and one primary plane where the selected
KMS API requires it. Atomic KMS is preferred only after complete property
discovery and a successful TEST_ONLY modeset; a pre-modeset legacy fallback is
available. Direct sessions own an exact Linux VT and DRM master, while inherited
sessions leave VT and master ownership external. Shutdown restores and reads
back saved KMS state before removing Glasswyrm framebuffers, then restores DRM
master and terminal ownership in order. The detailed contract is recorded in
[`docs/output/`](output/).

Milestone 12 keeps that authority split while making the CPU-buffer path
explicit and damage-aware. An opt-in game profile in `glasswyrmd` owns the
bounded X11 extension, colormap, and EWMH behavior; `gwm` owns fullscreen,
borderless, restore-geometry, stacking, and focus policy; and `gwcomp` selects
the scalar reference or constrained EGL/GLES scene renderer before presenting
the same component-neutral frame. Additive GWIPC API 0.7 eventfd readiness
orders producer writes before compositor reads, and completed DRM buffer
generations bound the scanout copy region. This profile adds no client GPU API,
direct scanout, output reconfiguration, or second display authority.

The process boundaries must carry explicit metadata. It is not acceptable for `glasswyrmd` or `gwm` to reduce a surface to only "draw this window here" information.

The server/WM policy contract should include at least:

- Map, unmap, configure, focus, stacking, and visibility state.
- Window type, transient relationship, override-redirect state, and decoration eligibility.
- Fullscreen, maximize, minimize, workspace, and attention/urgency state.
- Client geometry requests and WM-applied geometry decisions.
- Presentation-relevant policy hints, such as fullscreen and direct-scanout eligibility.

The compositor-facing surface contract should include at least:

- Surface identity and parent/window association.
- WM-applied stacking, clipping, decoration, and visibility state.
- Buffer handle or storage reference.
- Buffer format and modifier when applicable.
- Damage region.
- Transform, opacity, clipping, and stacking state.
- Synchronization/fence state when applicable.
- Scale metadata.
- Color space, transfer function, primaries, and luminance metadata.
- HDR metadata such as mastering display data, MaxCLL, and MaxFALL when available.
- Presentation timing hints.
- Fullscreen/direct-scanout eligibility hints.

The compositor should be able to reject, downgrade, or log incomplete metadata rather than silently guessing for modern display features.

Traditional split must not mean `gwcomp` is an ordinary X11 client that draws into a server-owned output. `gwcomp` is a privileged, co-developed display engine. Owning final composition remains essential for HDR, VRR, and scaling policy.

The architectural rule is:

> The X11 server owns protocol truth. The window manager owns policy truth. The compositor owns photons.

## 11. Protocol strategy

### 11.1 Core X11

Initial work should implement only enough core X11 to support the compatibility tiers.

Required early features:

- Unix-domain socket listener.
- X11 setup handshake.
- Client byte-order handling.
- Resource ID allocation and validation.
- Basic error replies.
- Basic atoms and properties.
- Window creation/destruction.
- Map/unmap.
- Configure/move/resize.
- Expose events.
- Basic input events.
- Basic event masks.
- Pixmap/drawable handling sufficient for early clients.

### 11.2 Extensions

Extensions should be implemented in strict priority order. Each extension may be a subset at first, but the subset must be documented.

Early likely extension subset:

- `BIG-REQUESTS`
- `MIT-SHM`
- `XFIXES`
- `DAMAGE`
- `Composite`
- `RANDR` subset
- `RENDER` subset
- `PRESENT` subset later
- `DRI3` much later
- `GLX` later, if needed

Custom Glasswyrm extensions should live under `GW_*` names. They should be explicitly experimental and versioned.

### 11.3 Code generation

Use generated packet definitions where practical.

Recommended approach:

- Use `xcb-proto` XML as a reference source.
- Generate Rust packet/domain metadata, decoder tables, and test vectors where
  useful; retain C declarations only for an intentional installed ABI or
  temporary migration bridge.
- Keep generated code isolated under a clear path.
- Preserve licenses for any vendored protocol definitions.
- Document regeneration commands.
- Avoid manually transcribing large protocol tables unless there is no reasonable alternative.

## 12. Windowing and compositor model

Glasswyrm should treat windows as protocol-visible objects and surfaces as compositor-visible renderable objects.

Suggested concepts:

- `gw_client`: connected protocol client.
- `gw_resource`: ID-backed protocol resource.
- `gw_window`: X-visible window object.
- `gw_surface`: compositor surface for renderable content.
- `gw_buffer`: pixel/storage object backing a surface.
- `gw_scene`: compositor scene graph.
- `gw_output`: physical/logical display output.
- `gw_seat`: input seat.

In the traditional Glasswyrm split, `gw_window` is owned by `glasswyrmd`. `gwm` consumes window state and produces policy decisions for focus, stacking, placement, workspaces, and decorations. `gwcomp` consumes surface, buffer, output, and WM policy state to build the final scene and present it.

Shared structures and messages must be treated as versioned API contracts across process boundaries rather than convenient private implementation details. The first implementation may keep the boundary simple for development, but it must not bake X11 protocol handling or WM policy directly into final composition policy.

`gwcomp` should remain separable from `glasswyrmd` and `gwm` without changing X11-visible behavior, apart from explicit policy differences selected by the active window manager.

The scene graph should support:

- Window stacking after WM policy is applied.
- Damage tracking.
- Output assignment.
- Per-output transforms.
- Per-surface scale metadata.
- Per-surface color metadata.
- Per-surface presentation metadata from `glasswyrmd` and `gwm`.
- Import/update events from the server/WM/compositor boundary.
- Frame scheduling.

## 13. Output model

Every output should have explicit state:

- Connector identity.
- Mode list.
- Current mode.
- Physical dimensions.
- Logical position.
- Logical scale.
- Fractional scale.
- Transform/rotation.
- HDR capability.
- Color characteristics.
- VRR capability.
- Current VRR policy.
- Current color/HDR policy.

`gwout` should eventually expose output configuration, similar in spirit to `xrandr`, but designed around Glasswyrm's internal model.

## 14. Per-output scaling model

Per-output scaling is a first-class design goal.

Glasswyrm should separate:

- Physical pixels.
- Logical desktop coordinates.
- Client buffer scale.
- Output scale.
- Compositor fallback scale.

Legacy X11 clients should continue to receive usable geometry. If a client is not scale-aware, Glasswyrm may compositor-scale its surface, accepting blur as a compatibility fallback.

Scale-aware clients should eventually use the `GW_SCALE` extension to receive:

- Current output membership.
- Preferred logical scale.
- Fractional scale information.
- Scale-change events when crossing outputs.
- Buffer scale negotiation.

Early `GW_SCALE` design should prefer simple, testable semantics over perfect toolkit integration.

## 15. HDR and color model

HDR is a first-class long-term goal, but initial work should focus on a safe SDR pipeline and explicit metadata plumbing.

The traditional server/WM/compositor split does not make HDR harder by itself as long as `gwcomp` remains the only final display authority. It does make every boundary more important: HDR-relevant surface attributes must flow through or around WM policy without being flattened into legacy pixmaps, guessed from window type, or hidden from tracing tools.

Recommended HDR stages:

1. SDR-only software compositor in `gwcomp`.
2. Output capability discovery in `gwcomp`.
3. Color metadata structures shared across the server/WM/compositor contracts.
4. IPC transport for per-surface color/HDR metadata, buffer format, modifier, damage, presentation state, and WM policy hints that may affect fullscreen or direct scanout without changing color interpretation.
5. `GW_COLOR` / `GW_HDR` protocol sketches.
6. Fullscreen HDR passthrough experiment.
7. Composited HDR experiment with mixed SDR/HDR surfaces.
8. SDR-to-HDR tone mapping.
9. Client/toolkit integration work.

Surface metadata should eventually include:

- Color space.
- Transfer function.
- Primaries.
- Buffer format, bit depth, and modifier when applicable.
- Alpha semantics.
- Luminance information.
- Mastering display metadata where applicable.
- MaxCLL / MaxFALL where applicable.
- SDR/HDR intent.

Output metadata should eventually include:

- Supported color spaces.
- HDR capability.
- Max/min luminance.
- Current output transform.
- Current HDR metadata state.

Do not claim full desktop HDR until SDR/HDR composition, tone mapping, metadata propagation across the server/WM/compositor boundaries, WM fullscreen/direct-scanout policy, and output behavior are tested on real HDR displays.

## 16. VRR model

VRR should be pursued before full HDR because it has a more direct KMS policy path and produces useful gaming-focused results earlier.

Recommended policy modes:

- `off`
- `fullscreen`
- `focused`
- `app-requested`
- `always-eligible`

Milestone 14 implements this model through three explicit authorities:

- `glasswyrmd` owns application preference, committed per-output policy,
  client-visible `GW_VRR` state, and transaction promotion;
- `gwm` classifies fullscreen, exact borderless fullscreen, focus, output
  membership, and application preference and selects at most one candidate per
  output; and
- `gwcomp` owns final surface validation, simulated or atomic-KMS application,
  effective-state readback, presentation timing, VT behavior, and restoration.

GWIPC API 0.9 transports independently negotiated VRR metadata, policy, and
presentation-timing records. It retains SOVERSION 0, wire version 1.0, and
every API 0.1-through-0.8 symbol and record. Experimental `GW_VRR` 0.1 lets a
repository-owned X11 client set Default, Disable, Allow, or Prefer on an owned
top-level window and observe committed state. It does not give the client KMS
authority, PRESENT timing, pacing, DRI3, DMA-BUF, or persistent configuration.

The implemented output policies are `off`, `fullscreen`, `focused`,
`app-requested`, and `always-eligible`. Candidate policies require one visible,
focused, managed window exclusively assigned to the output and reject explicit
Disable. Fullscreen accepts applied EWMH fullscreen or the exact complete-output
borderless classification. AppRequested additionally requires Prefer.
AlwaysEligible is an administrative override that does not require a window
candidate.

Headless simulation is enabled only by a repeated
`gwcomp --headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ` declaration for a named
headless output. `gwcomp --vrr-report PATH` publishes bounded capability,
decision, timing, summary, and restore records without changing pixel output.
`glasswyrmd --output-model --control-socket PATH --vrr-protocol` enables the
server side; `--vrr-protocol` without the output model is rejected. `gwout set
OUTPUT --vrr MODE` changes policy through the complete-layout transaction, and
`gwinfo vrr [OUTPUT]` reports state, reasons, and timing. Historical
`gwinfo outputs`, `windows`, and `all` preserve their exact schema unless the
caller explicitly adds `--vrr`, which appends nested per-object VRR state.

The physical path is deliberately narrower: one reviewed atomic-KMS connector,
one CRTC, the composited primary-plane XRGB8888 path, scale 1, transform Normal,
and exact readback. Connector `vrr_capable`, CRTC `VRR_ENABLED`, and successful
TEST_ONLY off/on commits establish controllability, not positive behavior.
Positive acceptance additionally requires enough monotonic kernel page-flip
samples to distinguish the same in-range cadence with VRR off and on, followed
by exact KMS/KD/VT/getty restoration. A driver may report sequence zero for
consecutive valid flip events; raw zero is retained, and only strictly
increasing kernel timestamps may establish chronology in that profile.

M14 host, fake-DRM, raw-protocol, and simulated-output coverage is implemented.
The clean QXL negative-capability gate and the separate reviewed physical
positive gate are still pending; consequently this specification does not yet
claim M14 hardware acceptance. Multi-physical-output VRR, mixed-refresh
optimization, legacy-KMS VRR, direct scanout, PRESENT/DRI3 timing,
vendor-specific APIs, HDR/color interaction, and toolkit integration remain
unsupported.

## 17. Rendering strategy

Start with a software renderer and headless framebuffer.

Reasons:

- Deterministic tests.
- Easier protocol bring-up.
- Easier pixel golden tests.
- No GPU-specific failure mode during early protocol work.
- Makes assembly optimization meaningful later.

Recommended render path stages:

1. Software framebuffer in headless backend.
2. PNG dump or raw frame dump for tests.
3. Damage-region copy/blit.
4. Alpha blending.
5. Software scaling.
6. x86_64 optimized variants after reference paths are stable.
7. DRM dumb-buffer display path.
8. GBM/EGL path.
9. Vulkan or advanced renderer later.

## 18. Input strategy

Input should use `libinput` for real devices.

Early stages:

1. Synthetic input in headless tests.
2. Basic pointer events.
3. Basic keyboard events.
4. Minimal keymap handling.
5. XKB-compatible behavior later.
6. Real libinput backend.
7. Seat abstraction.

Do not let full XKB complexity block early window/protocol milestones. Keyboard handling should be improved in layers.

## 19. Security and session model

Early Glasswyrm should be local-only.

Rules:

- Use Unix-domain sockets initially.
- Disable TCP listening by default.
- Do not install setuid binaries.
- Do not require root to run long-term.
- Support both systemd-logind and non-systemd permission paths such as seatd, udev rules, or explicit launcher-mediated device access.
- Treat X11-compatible behavior as inherently permissive; document this honestly.
- Do not claim Wayland-like isolation without an explicit security design.

GWIPC listeners and clients authenticate local peers with `SO_PEERCRED` and
reject a different effective UID. Descriptor-bearing buffer records validate
the received object type, access mode, declared storage extent, and eventfd
shape before exposing descriptors to compositor code. Filesystem endpoints and
evidence paths must reject unsafe writable ancestors and symbolic-link
replacement rather than relying on a path-only check followed by `open`.

The transition server bounds each X11 setup handshake to five seconds and 128
concurrent client workers. Its legacy resource model additionally caps the atom
table at 65,536 entries and 4 MiB of atom-name bytes, and caps windows at
32,768 per client and 65,536 globally. These are availability controls, not
per-client X11 isolation guarantees.

Legacy request processing also accounts semantic work that is not represented
by wire size. MIT-SHM image work yields after 64 MiB per client turn, RENDER
clip and raster work is bounded before pixel mutation, passive button grabs are
capped at 1,024 per client and 4,096 globally, and cursor image quota remains
charged while any server object retains the image.

The Rust compositor bounds snapshots, live buffers, pending releases, frame
evidence, and VRR timing retention. Frame dumping is opt-in for ordinary runs;
evidence exhaustion disables further writes without terminating presentation.
Absolute handshake, initial-frame, and snapshot deadlines prevent one stalled
same-UID producer from retaining the compositor endpoint indefinitely.

The first security objective is not perfect isolation. It is to avoid unnecessary old X server attack surface.

## 20. Gentoo integration

Gentoo is the primary target distribution.

Recommended Gentoo plan:

- Keep upstream project source clean first.
- Add `packaging/gentoo` once the basic build works.
- Maintain a local overlay under `packaging/gentoo/overlay/` once ebuild work begins.
- During migration, keep live ebuilds pinned and explicit about their Cargo and
  Meson component paths; do not publish a final build arrangement until it
  stabilizes.
- Prefer release-tarball ebuilds for reproducible VM tests once releases exist.
- Do not replace system Xorg automatically.
- Provide clear install/remove/rollback notes.
- Keep experimental USE flags explicit.
- Keep runtime and packaging usable with both systemd and OpenRC.
- Do not make systemd a hard runtime dependency; systemd units may be provided only alongside equivalent OpenRC init/session guidance.
- Test or document both init-system paths before claiming Gentoo desktop usability.

Possible packages:

```text
x11-base/glasswyrm       # metapackage or session bundle
x11-base/glasswyrmd      # X11-compatible server process
x11-wm/gwm               # Glasswyrm window manager and policy process
x11-base/gwcomp          # compositor, renderer, and display authority process
x11-apps/gw-tools        # gwctl, gwinfo, gwtrace, gwout, gwbench
gui-libs/libgwipc        # internal IPC contracts shared by runtime components
gui-libs/libgwproto      # protocol helpers, if installed as a shared library
gui-libs/libgwrender     # renderer helpers, if installed as a shared library
```

The final category split can be changed once the repository structure settles.

### 20.1 Split package semantics

The package split should reduce rebuild, install, and update scope. It should
not be treated as a guarantee that Portage fetches less source by itself.
Multiple ebuilds may still consume the same upstream source tree. Use a shared
release tarball, shared `DISTDIR`, or intentional local git cache/mirror when
multiple packages are built from the same revision.

The first split worth preserving is the runtime authority split:

- `glasswyrmd` for X11 protocol/server behavior.
- `gwm` for window-management policy.
- `gwcomp` for final composition and display authority.
- `libgwipc` for versioned process-boundary contracts.

`gwm` and `gwcomp` should be separately buildable and installable because they
will likely churn for different reasons. A window-manager policy update should
not rebuild the compositor or server unless an installed shared library or IPC
ABI changed. A compositor renderer/KMS update should not rebuild `gwm` unless
WM/compositor policy contracts changed.

The split is not complete until the active Cargo/Meson arrangement exposes
narrow build/install targets or options for `glasswyrmd`, `gwm`, `gwcomp`,
tools, and installed libraries. Component ebuilds should use those targets
instead of compiling the full stack and discarding unrelated install artifacts.

`libgwipc` should be treated as the first serious ABI-bearing library. Until the
contract stabilizes, runtime components should depend on a matching version of
`libgwipc`. Once ABI rules are real, use Gentoo slot or subslot semantics rather
than allowing silent drift.

Avoid splitting every internal helper library before APIs harden. `libgwproto`
and `libgwrender` may become packages if they are installed and shared by more
than one component; otherwise they can remain internal implementation details.

### 20.2 Local overlay and fresh VM validation

Codex should maintain a local ebuild repository that can be handed to a fresh
Gentoo VM. The recommended in-repo shape is:

```text
packaging/gentoo/overlay/
  profiles/
    repo_name
  metadata/
    layout.conf
  x11-base/
    glasswyrm/
    glasswyrmd/
    gwcomp/
  x11-wm/
    gwm/
  x11-apps/
    gw-tools/
  gui-libs/
    libgwipc/
    libgwproto/
    libgwrender/
```

The VM should consume that overlay through `repos.conf`, not by copying files
into the main Gentoo repository. A typical manual registration is:

```sh
mkdir -p /etc/portage/repos.conf
cat >/etc/portage/repos.conf/glasswyrm-local.conf <<'EOF'
[glasswyrm-local]
location = /mnt/shared/glasswyrm-overlay
masters = gentoo
auto-sync = no
EOF
emerge --metadata
```

A shared directory is useful for passing the overlay, source tarballs, distfiles,
binary packages, logs, and test reports into or out of the VM. It must not be
the only validation path. The fresh VM test should exercise Portage dependency
resolution, USE flags, Cargo features and Meson component options where
applicable, install paths, service/session files, and uninstall behavior.

Recommended VM checks:

```sh
emerge --pretend --verbose --tree x11-base/glasswyrm
emerge -av x11-base/glasswyrm
emerge --pretend --verbose --tree x11-wm/gwm
emerge -1av x11-wm/gwm
emerge -C x11-wm/gwm
```

For narrow-update tests, bump only the target component ebuild revision and run
`emerge --pretend --verbose --tree` before building. A `gwm` revision bump should
not rebuild `glasswyrmd` or `gwcomp` unless `libgwipc` or another shared ABI has
changed. If binary packages are tested, generate them through Portage, for
example with `FEATURES=buildpkg`, rather than copying untracked binaries into
the VM.

Live ebuilds should pin `EGIT_COMMIT` for reproducible VM validation unless the
test is explicitly about current `main`. Release ebuilds should prefer a shared
source tarball so split packages reuse the same cached distfile.

## 21. Testing strategy

Testing is mandatory from the first implementation sprint.

Tests are organized by the cheapest tier capable of disproving a change:

1. **Tier 0 — compile/type feedback:** targeted Cargo check, format, lint, and
   retained native compilation. No process or fixture use.
2. **Tier 1 — unit/pure policy:** codecs, geometry, scaling, resource state,
   focus/stacking, output/VRR decisions, damage, software rendering, and fake
   backend behavior.
3. **Tier 2 — contract/component:** canonical GWIPC bytes, C/Rust
   interoperability, installed ABI, malformed input, FD lifecycle,
   snapshot/replay, and structured fixture validation.
4. **Tier 3 — headless process integration:** only the processes required for
   the scenario, using explicit readiness, deterministic teardown, and
   per-process failure artifacts.
5. **Tier 4 — full software acceptance:** all earlier tiers plus compatibility
   fixtures, software goldens, strict builds, sanitizers, install/package
   smoke, and applicable Gentoo/QXL VM gates.
6. **Tier 5 — physical hardware acceptance:** explicitly authorized,
   preflighted DRM/KMS and VRR validation against already-built artifacts.

Run the narrow test first and broaden only at coherent checkpoints. A harness,
readiness, timeout, parser, fixture, or expected-value bug is a first-class
defect: add a focused harness regression and do not repeat physical validation
merely because harness interpretation changed.

Never move the implementation and oracle together. Port a test so it first
passes against the legacy implementation, preserve its accepted fixture, then
point the same test at the Rust replacement. Do not serialize Rust memory
layout as GWIPC. Message numbers, integer widths, byte order, framing, version
negotiation, FD ownership, ordering, and snapshot/replay semantics are explicit
Tier 2 contracts.

Process tests should use observable readiness such as a successful socket/GWIPC
handshake or expected snapshot, not a fixed sleep as the sole proof. On failure,
retain commands, individual stdout/stderr, exit/signal status, monotonic event
timing, structured traces/snapshots, configuration, and fixture checksums.

Real DRM/KMS is not a normal development prerequisite. During the Rust
transition, any command capable of live display takeover or physical testing
must fail closed unless `GW_ALLOW_HARDWARE_TESTS=1` is set to the exact value
`1`. Ordinary test wrappers omit it. Offline doctor, dry-run, self-test,
analysis, recorded-state replay, fake DRM, headless simulation, and QXL do not
need this authorization, but none provides positive physical VRR proof.

Gentoo packaging tests should not be replaced by shared-directory artifact
copies. Shared directories may provide an overlay, distfiles, binary packages,
and logs, but at least one fresh VM path should run `emerge` against the local
overlay so the ebuilds, dependencies, USE flags, and install layout are tested.
Use the `glasswyrm` VM for applicable packaging, QXL, VT/restart, and supported
client gates that do not require real hardware, after the local software tiers
are green.

The detailed transition classification and migration topology are maintained
in `docs/maintenance/RUST_TRANSITION_TEST_MAP.md`.

## 22. Logging, diagnostics, and tracing

Observability is a core requirement.

Recommended log areas:

- `protocol`
- `client`
- `resource`
- `window`
- `wm`
- `compositor`
- `ipc`
- `render`
- `input`
- `output`
- `drm`
- `vrr`
- `hdr`
- `scale`

`gwtrace` should eventually support:

- Client connection tracing.
- Request/reply/event traces.
- Resource lifetime traces.
- WM policy decision traces.
- Server/WM/compositor IPC traces.
- Surface metadata snapshots.
- Frame scheduling traces.
- Damage visualization.
- VRR eligibility logs.
- Output state snapshots.

Early logging can be simple, but it must be consistent.

## 23. Configuration

Initial configuration should be simple and explicit.

Recommended early config format: TOML or INI-like file. The final choice can be deferred.

Configuration should eventually include:

- Enabled backend.
- Output layout.
- Output scale.
- Window manager selection or built-in policy mode.
- VRR policy.
- HDR policy.
- Renderer selection.
- Assembly optimization policy.
- Log levels.
- Socket path.
- Experimental extension toggles.

The current M14 command-line configuration is intentionally explicit:

- `glasswyrmd --output-model --control-socket PATH --vrr-protocol` enables the
  experimental X11 view and same-UID control surface;
- `gwcomp --headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ` enables bounded
  per-output simulation;
- `gwcomp --vrr-report PATH` creates a non-replacing JSONL report;
- `gwcomp --mirror-dump-trigger PATH` optionally gates physical mirror PPMs
  behind one-shot regular-file markers while leaving the historical per-frame
  mirror behavior unchanged when omitted; and
- `gwout set OUTPUT --vrr MODE` is the only implemented persistent-process
  policy edit. Policy is not persisted across a new session.

Environment variables may be used for developer overrides, but should not become the primary configuration interface.

## 24. Commit and branch workflow

Implementation work should commit often.

Rules:

- Commit small, coherent changes.
- Multiple commits per implementation task are encouraged.
- If a task touches unrelated areas, split commits by area.
- Keep build fixes separate from feature commits where practical.
- Keep documentation updates close to the implementation they describe.
- Push in bulk only once the task is complete and validated.
- Do not force-push shared branches unless explicitly instructed.
- Do not rewrite history without explicit permission.

Recommended commit message style:

```text
area: short imperative summary

Optional body explaining why and how.
```

Examples:

```text
protocol: add setup handshake parser
compositor: add headless framebuffer scene target
render: add ARGB over XRGB reference blend path
docs: record initial protocol compatibility tiers
```

## 25. Milestone roadmap

The implementation roadmap is intentionally split into narrow, independently
testable milestones. Completed milestones describe only behavior proven by the
repository's tests and acceptance harnesses.

```text
M0  Repository skeleton                         complete
M1  X11 setup service                           complete
M2  Core protocol and resources                 complete
M3  Versioned IPC foundation                    complete
M4  Headless compositor and synthetic surfaces   complete
M5  Window-manager policy scaffold              complete
M6  Three-process mapped-window lifecycle       complete
M7  Drawable and software-rendering bridge        complete
M8  Synthetic input and event routing             complete
M9  Simple real X11 clients                     complete
M10 DRM/KMS software scanout                    complete
M11 Interactive desktop baseline                complete
M12 Efficient buffers and game-oriented clients        complete
M13 Output model and per-output scaling          complete
M14 Variable refresh rate                       implementation present; acceptance pending
R   Rust-first runtime transition              active; M15 feature freeze
M15 Color management and HDR                   paused until transition acceptance
M16 Toolkit and daily-driver expansion
```

Milestone 4 proves the compositor-facing architecture with a synthetic producer
and deterministic headless output. It does not connect `glasswyrmd` or `gwm`,
map X11 windows, route input, or access display hardware. Those boundaries stay
deferred to the later milestones listed above.

Milestone 5 implements a separate `gwm` policy service and an additive GWIPC
API 0.3 WindowPolicy vocabulary while retaining SOVERSION 0 and wire 1.0. Host
and Gentoo VM tests prove deterministic policy evaluation, transactional
state, public snapshot controls, codec goldens, process behavior,
malformed-peer isolation,
and the fixed VM harness. The terminal-only acceptance gate passes with Xorg
and Xwayland absent. `glasswyrmd`, `gwm`, and `gwcomp` remain disconnected;
X11 mapping and three-process lifecycle begin in M6.

Milestone 6 implementation now connects `glasswyrmd` explicitly to `gwm` and
`gwcomp`, adds API 0.4 lifecycle records, metadata-only compositor surfaces,
deferred per-client lifecycle barriers, and structural event routing. It
is accepted by repository-owned raw/XCB/restart probes, golden M6 fixtures, the
Gentoo VM scenario, sanitizer testing, and the full component build matrix.
This status does not imply drawing, input, or normal X11 application
compatibility.

Milestone 7 adds an explicit `--software-content` integrated profile. It keeps
canonical depth-24 XRGB8888 pixels in `glasswyrmd`, implements the documented
Pixmap, GraphicsContext, GXcopy, plane-mask, image-upload, fill, copy, clear,
and exposure subset, and publishes synchronized read-only memfd mirrors through
the existing GWIPC 0.4/wire 1.0 buffer and damage contracts. `gwcomp` remains
the final composition authority. Repository-owned raw/XCB/restart probes,
reviewed pixel evidence, strict compiler builds, sanitizers, component builds,
and the fixed Gentoo VM scenario define the accepted boundary; input, fonts,
child composition, acceleration, and normal application compatibility remain
deferred.

Milestone 8 adds explicit `--synthetic-input-socket` integrated mode and GWIPC
API 0.5 DiagnosticTool motion, button, raw-keycode, barrier, and acknowledgement
records while preserving SOVERSION 0 and wire 1.0. `glasswyrmd` owns input
state, one-layer top-level hit testing, X11 event selection/propagation, fixed
modifier state, crossing, and event encoding. Button 1 click focus is committed
through the existing GWM lifecycle policy transaction and projected to
`gwcomp`. Repository raw/XCB/restart probes, reviewed event and pixel fixtures,
strict builds, sanitizers, component builds, and the fixed Gentoo VM scenario
define the accepted boundary. Real devices, grabs, cursors, XKB, mapping
requests and child/InputOnly hit testing remain unsupported.

Milestone 9 establishes the first command-specific external application tier:
unmodified `xeyes` 1.3.1 and `xclock` 1.2.0 run under the pinned profiles with
reviewed exact frames and normalized traces. The implementation adds the
bounded fixed-font, depth-1 bitmap, child composition, core raster, color,
mapping, pointer, and coordinate behavior required by those profiles while
keeping Shape, Render, XKB, real devices, and broad toolkit compatibility out
of scope. Strict builds, sanitizers, component matrices, restart checks, and
the terminal-only Gentoo VM gate define the accepted boundary.

Milestone 10 adds a Linux-only, opt-in DRM/KMS presentation backend below the
existing deterministic compositor. It preserves headless as the default and
uses two linear XRGB8888 dumb buffers, exact-size single-output selection,
verified atomic modesetting with a documented legacy fallback, delayed frame
acknowledgement/release, direct and inherited session boundaries, process-mode
VT switching, and ordered KMS/KD/VT restoration. DRM and VT behavior has
deterministic fake-backed host coverage and a fixed Gentoo VM acceptance route.
The configured QXL guest validates the real primary node, atomic KMS
presentation, exact graphical-console screenshots, VT release/acquire,
post-acquire repaint, ordered restoration, and the checksum-protected evidence
archive.

Milestone 11 adds an opt-in libinput path backend and libxkbcommon US pc105
engine inside `glasswyrmd`, preserving the M8 synthetic profile. It adds
bounded core cursor, grab, selection, keyboard-control, and client-event
behavior; capability-gated session-state and GWM interactive bindings through
GWIPC API 0.6; a software cursor composed by `gwcomp`; and the unprivileged
`glasswyrm-session` orchestrator. The intended external target is only xterm
patch 410 under the pinned core-font ASCII profile. Two full bootstrap runs at
`eb8a20f76b24cc7c07459a402603bad5e7b6cc39` reproduced the presence-normalized
live trace and validated typing, scrolling, selection exchange, interactive
move/resize/close, VT and compositor-restart recovery, canonical DRM frames,
graphical-console screenshots, ordered restoration, and evidence-archive
integrity. The clean full exact run at
`53ec4879b858b96a9b7e8734fb173d037cbc683b` reproduced the committed fixture
with every summary field passing and no evidence errors, accepting this narrow
profile. The repository release sequence still repeats the host matrix and
clean M10/M11 VM order at the final documentation HEAD.
Passive grabs cover only the observed `GrabButton` path; `UngrabButton` and
passive key grabs remain unsupported. This is not a broader xterm, Xt/Xaw,
toolkit, Unicode, or X11 compatibility claim.

Milestone 12 implementation adds a static `--game-compat` registry with
bounded BIG-REQUESTS 1.0, MIT-SHM 1.1, XFIXES 2.0, DAMAGE 1.1, RENDER 0.11,
COMPOSITE 0.4, and RANDR 1.3 subsets. It also adds depth-8/depth-32 pixmaps,
client TrueColor colormaps, SDL-oriented EWMH fullscreen and borderless
policy, GWIPC API 0.7 eventfd CPU-buffer readiness, an internal renderer
abstraction, an opt-in EGL/GLES 2.0 compositor renderer, and completed-damage
DRM copies. Software rendering remains the default and canonical reference;
wire 1.0 and SOVERSION 0 remain unchanged. The only external target is the
exact SDL 2.32.10 X11 software-renderer profile with the pinned repository
probe and official `testdraw2`/`testsprite2` workloads. Host implementation
tests and evidence validators do not accept that target by themselves. The
accepted profile is proved by the clean M11-to-M12 Gentoo VM sequence,
software/GLES and DRM image evidence, real interaction, restart/VT recovery,
restoration, cleanup, and archive gates.

Milestone 13 implementation adds an opt-in compositor-authoritative output
model with stable output and mode identities, several bounded logical headless
outputs, one physical DRM output, atomic generation-guarded layout changes,
rational compositor scaling, all eight output transforms, surface membership,
and one-workspace multi-output GWM policy. GWIPC API 0.8 retains SOVERSION 0
and wire 1.0 while adding output inventory, policy, membership, and same-UID
control contracts. RANDR 1.3 remains read-mostly; experimental `GW_SCALE` 0.1
is proven only by the repository client. Software remains the canonical
renderer. The bounded profile is accepted by the historical M12 gate followed
by the clean M13 Gentoo VM sequence, including headless output, scaling,
transform, RANDR, `GW_SCALE`, software/GLES, and replay evidence plus the
one-output QXL DRM scale/transform, VT/input recovery, restoration, cleanup,
and archive gates. Host implementation tests alone do not accept this profile.

The exact M13 compatibility boundary is:

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

Milestone 14 implementation adds the bounded VRR model described in Section
16. It extends GWIPC API 0 to 0.9 without changing wire 1.0 or SOVERSION 0,
adds deterministic GWM policies and server-owned application preference,
keeps final effective state in `gwcomp`, adds atomic `VRR_ENABLED` control and
kernel page-flip timing, supports deterministic headless simulation, and
exposes `gwout`, `gwinfo`, and experimental `GW_VRR` 0.1 interfaces. The
host/fake/simulated proof is present, but neither the required clean QXL gate
nor the separate physical-display positive gate is recorded as accepted yet.

The Rust transition preserves M14 behavior against anchor
`36009a8bbe50794d6808d142ac57b623062a1e63` while carrying forward the later
software/diagnostic source at
`a39c7788bb5226e793698192028b9a4a57202f8d`. These identifiers have different
roles and must not be conflated. Milestone 15 implementation is frozen until
the Rust stack passes full software acceptance and bounded M14 physical
re-acceptance. Critical correctness fixes remain allowed.

The candidate M14 release boundary is:

Supported by the implementation:

- DRM connector `vrr_capable` discovery
- atomic CRTC `VRR_ENABLED` control
- one physical VRR output
- composited primary-plane page flips
- off/fullscreen/focused/app-requested/always-eligible policies
- borderless fullscreen
- `gwout` manual policy
- `gwinfo` reasons and timing
- experimental `GW_VRR` 0.1 client preference
- simulated headless VRR policy
- VT and peer restart recovery

Unsupported:

- VRR on legacy KMS
- several physical VRR outputs
- mixed-refresh optimization
- direct scanout
- PRESENT/DRI3 timing
- vendor-specific VRR APIs
- VRR with HDR/color management
- toolkit integration

The QXL negative-capability gate is fixed to the internal snapshot named
`base` and must run after the accepted M13 gate with a reset between them:

```sh
./tools/gw-vm reset --yes
./tools/gw-vm milestone13-runtime-test --yes
./tools/gw-vm reset --yes
./tools/gw-vm milestone14-runtime-test --yes
```

The physical gate is intentionally separate. It can take DRM master, switch
VTs, stop the selected getty, and reconfigure the reviewed display. It must not
be run from an active graphical session or from a TTY whose interruption is
unacceptable:

```sh
tested_commit=$(git rev-parse HEAD)
meson setup /var/tmp/glasswyrm-build-m14 \
  --buildtype=debugoptimized \
  -Ddrm_backend=true -Dlibinput_backend=true \
  -Dphysical_validation_provenance=true
meson compile -C /var/tmp/glasswyrm-build-m14
./tools/gw-hw doctor --config /path/to/reviewed.toml \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-doctor
GW_ALLOW_HARDWARE_TESTS=1 \
systemd-run --scope --unit=glasswyrm-m14-harness \
  --collect --quiet -- \
  ./tools/gw-hw milestone14-vrr-test \
  --config /path/to/reviewed.toml \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-hardware --yes
```

The physical harness fails closed unless the fixed build contains the
Meson-generated provenance manifest for the exact clean tested commit and the
current sizes and SHA-256 hashes of all seven repository executables match it.
The validated manifest is part of the checksummed evidence archive.
The live command additionally requires `GW_ALLOW_HARDWARE_TESTS` to equal
exactly `1`; unset and malformed values are rejected before the tool validates
the live scope, creates artifacts, accesses devices, or begins session
takeover. Doctor, dry-run, self-test, analysis, and fixture replay remain
offline and do not require the opt-in.
Direct live execution is rejected before artifact creation or hardware
takeover: the fixed transient scope keeps the harness alive when it stops the
configured getty and retains the unconditional restoration guard.

## 26. Definition of done

For any implementation task, done means:

- The code builds.
- Relevant tests pass.
- New behavior has tests or a documented reason tests are not yet possible.
- Documentation is updated if behavior, architecture, or workflow changes.
- Logging/tracing is adequate for debugging the new behavior.
- No unrelated formatting churn is mixed into the change.
- Each commit is coherent and reviewable.
- The final task state is ready to push in bulk.

## 27. Open questions

These should be resolved through future design notes or implementation experience:

- Whether to use CMake instead of Meson if project needs change.
- Whether to vendor `xcb-proto` XML or require it as a build-time dependency.
- Whether a nested X11 backend remains useful after the M10 DRM/KMS path.
- Whether a renderer beyond the bounded M12 EGL/GLES compositor is useful.
- Whether Vulkan should be a serious early render backend or postponed.
- Exact configuration format.
- How much ICCCM/EWMH behavior to implement before toolkit work.
- How to handle XKB compatibility without swallowing the whole swamp at once.
- Exact Gentoo split-package versioning and slot/subslot policy.
- Whether release tarballs, pinned live ebuilds, or both should be the primary VM test path.
- Long-term security model beyond local-only X11-compatible behavior.
