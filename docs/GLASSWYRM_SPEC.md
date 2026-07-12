# Glasswyrm Project Specification

Status: Initial planning specification  
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
- C, C++, and selective x86_64 assembly.
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

Glasswyrm should use a deliberate mix of C, C++, and x86_64 assembly.

### 4.1 C

Use C for:

- DRM/KMS backend glue.
- libinput and udev integration.
- Low-level platform interfaces.
- C ABI boundaries between major subsystems.
- Small compatibility libraries.
- Test fixtures where C makes protocol behavior easier to inspect.

Recommended C dialect: C17 initially, with optional C23 only when build support is proven across Gentoo toolchains.

### 4.2 C++

Use C++ for:

- Server architecture.
- Resource lifetime management.
- Window and surface model.
- Compositor scene graph.
- Event routing.
- Output policy.
- Protocol dispatch tables.
- Renderer abstraction.
- Test harnesses where RAII improves cleanup.

Recommended C++ dialect: C++20 initially. C++23 may be adopted after compiler and standard library support is confirmed.

Recommended C++ style:

- Use RAII for file descriptors, DRM resources, mapped memory, client connections, and buffers.
- Prefer explicit ownership over hidden shared state.
- Keep module interfaces small.
- Avoid exceptions across subsystem boundaries.
- Avoid framework-heavy C++ patterns.
- Keep RTTI optional and avoid relying on it for core dispatch.
- Prefer `std::span`, `std::string_view`, and fixed-width integer types where appropriate.
- Prefer simple structs and explicit state machines over inheritance-heavy hierarchies.

### 4.3 x86_64 assembly

Assembly is allowed and encouraged only where it is useful, educational, and testable.

Use assembly for:

- Software compositor hot paths after a C/C++ reference implementation exists.
- Pixel blending.
- Pixel format conversion.
- Scaling blits.
- Color conversion experiments.
- Carefully isolated ABI experiments.
- Optional optimized protocol or copy routines after profiling.

Assembly rules:

- No assembly-only feature may exist without a portable C/C++ fallback.
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
- Standard C/C++ libraries.
- Common test libraries when justified.

Avoid or prohibit initially:

- Forking Xorg server code.
- Forking XLibre server code.
- Depending on wlroots.
- Depending on Weston, Mutter, KWin, or other compositor internals.
- Depending on Wayland protocols for core runtime behavior.
- Large third-party frameworks that obscure the display-stack internals.

## 6. Build system recommendation

Recommended build system: Meson + Ninja.

Reasons:

- Good fit for C, C++, and assembly.
- Common in the freedesktop/Linux graphics ecosystem.
- Fast incremental builds.
- Good support for `compile_commands.json`.
- Friendly to Gentoo ebuild packaging.
- Clean feature options for assembly paths, sanitizers, render backends, and experimental extensions.

The build should provide options similar to:

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

Early development should support both GCC and Clang where practical. CI or local test scripts should build at least one strict configuration and one sanitizer configuration.

Split-process options must not blur authority boundaries. A built-in WM policy
mode is acceptable only as an explicitly labeled development/test path; it must
not make `gwcomp` own X11 protocol semantics. A single-process debug build may
host multiple components in one process for tests, but it should still exercise
the same `libgwipc` message contracts used by the real split.

## 7. Repository layout

Recommended initial repository layout:

```text
/
  AGENTS.md
  README.md
  meson.build
  meson_options.txt
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

Process-specific directories may start as stubs. `src/compositor/`,
`src/backends/`, and `src/render/` are `gwcomp`-facing implementation areas
unless tests use them in isolation. `src/ipc/` owns versioned message contracts
shared across `glasswyrmd`, `gwm`, and `gwcomp`. Nested and GL/Vulkan backends
may remain placeholders until their phase begins.

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
- `COMPOSITE`
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
- Generate C/C++ packet structures, opcode metadata, decoder tables, and test vectors where useful.
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

Initial VRR work should target:

- Single-output fullscreen clients.
- Borderless fullscreen behavior.
- Debug logging of VRR eligibility.
- Manual override through `gwctl` or `gwout`.

Multi-output and composited mixed-refresh behavior should come later.

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

The first security objective is not perfect isolation. It is to avoid unnecessary old X server attack surface.

## 20. Gentoo integration

Gentoo is the primary target distribution.

Recommended Gentoo plan:

- Keep upstream project source clean first.
- Add `packaging/gentoo` once the basic build works.
- Maintain a local overlay under `packaging/gentoo/overlay/` once ebuild work begins.
- Provide live ebuilds only after Meson options stabilize.
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

The split is not complete until Meson exposes narrow build/install targets or
options for `glasswyrmd`, `gwm`, `gwcomp`, tools, and installed libraries.
Component ebuilds should use those targets instead of compiling the full stack
and discarding unrelated install artifacts.

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
resolution, USE flags, Meson component options, install paths, service/session
files, and uninstall behavior.

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

Test layers:

- Unit tests for protocol packet parsing.
- Unit tests for resource table behavior.
- Unit tests for event masks and dispatch.
- Unit tests for `gwm` policy decisions.
- IPC contract tests between `glasswyrmd`, `gwm`, and `gwcomp`.
- Metadata round-trip tests for scale, color, HDR, and presentation state.
- Pixel tests for software compositor output.
- Golden tests for C/C++ reference render paths.
- Golden tests for assembly render paths.
- Integration tests using toy clients.
- Headless compositor tests.
- Fuzzing for protocol decoders.
- Sanitizer builds.
- Fresh Gentoo VM packaging tests through a local overlay.
- Narrow component-update tests for `gwm`, `gwcomp`, and `libgwipc`.

Recommended early commands:

```sh
meson setup build -Dbackend_headless=true -Drender_software=true -Dasan=true -Dubsan=true
meson test -C build
```

Real DRM/KMS tests should not be required for normal CI-style validation. They should be explicit hardware tests.

Gentoo packaging tests should not be replaced by shared-directory artifact
copies. Shared directories may provide an overlay, distfiles, binary packages,
and logs, but at least one fresh VM path should run `emerge` against the local
overlay so the ebuilds, dependencies, USE flags, and install layout are tested.

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
M6  Three-process mapped-window lifecycle       in progress
M7  Drawable and software-rendering bridge
M8  Synthetic input and event routing
M9  Simple real X11 clients
M10 DRM/KMS software scanout
M11 Interactive desktop baseline
M12 Efficient buffers and game-oriented clients
M13 Output model and per-output scaling
M14 Variable refresh rate
M15 Color management and HDR
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
remains marked in progress until the repository-owned raw/XCB/restart probes,
golden M6 fixtures, Gentoo VM scenario, and full validation matrix pass. This
status does not imply drawing, input, or normal X11 application compatibility.

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
- Exact first subset of X11 requests needed for `xeyes` and `xclock`.
- Whether to support a nested X11 backend before real DRM/KMS.
- Whether first GL path should use EGL/GLES or desktop OpenGL.
- Whether Vulkan should be a serious early render backend or postponed.
- Exact configuration format.
- How much ICCCM/EWMH behavior to implement before toolkit work.
- How to handle XKB compatibility without swallowing the whole swamp at once.
- Exact Gentoo split-package versioning and slot/subslot policy.
- Whether release tarballs, pinned live ebuilds, or both should be the primary VM test path.
- Long-term security model beyond local-only X11-compatible behavior.
