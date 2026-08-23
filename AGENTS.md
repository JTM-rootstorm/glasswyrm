# Glasswyrm Agent Instructions

The workspace root `../AGENTS.md` applies here and owns coordination, worker topology, model allocation, external-write/VM boundaries, Git/commit/push policy, licensing selection, and general validation/security discipline. This file contains only Glasswyrm-specific technical doctrine.

Glasswyrm is a from-scratch, local-first, X11-compatible display stack for modern Linux/Gentoo. It is not a fork of Xorg, XLibre, Xwayland, wlroots, Weston, Mutter, KWin, or another display server/compositor stack.

## 1. Read order and authority

Before implementation, read:

1. `docs/GLASSWYRM_SPEC.md`
2. relevant committed design/decision documents under `docs/`
3. the existing subsystem code/tests being changed

A clearly newer committed design decision may supersede older spec text. Otherwise do not silently choose between spec/code contradictions; update the contract in scope or report the conflict.

## 2. Architecture doctrine

Glasswyrm is X11-compatible where useful, not Xorg-compatible by default.

The core split is:

- `glasswyrmd`: X11 protocol/resource truth
- `gwm`: window-management policy truth
- `gwcomp`: final composition/output authority

Keep those boundaries explicit. `gwcomp` must not become an ordinary X11 client or a legacy external compositor that merely assembles redirected pixmaps.

Do not import legacy behavior without a concrete compatibility target and test. Favor clean internal architecture, explicit compatibility tiers, and headless-testable policy.

Primary target:

- Gentoo Linux, x86_64
- local desktop sessions
- DRM/KMS for real display output
- libinput for real input

## 3. Language and dependency rules

New runtime code is Rust-first. Allowed exceptions:

- C for bounded platform/ABI shims where it is simpler and more auditable
- C++ only for retained migration code, temporary legacy/Rust comparison adapters, or unavoidable dependencies
- x86_64 assembly only for isolated profiled hot paths with a correct Rust/C reference implementation

Do not introduce another runtime implementation language without explicit approval. Python/shell may be used for justified build/test/generator helpers, not as Glasswyrm runtime components.

Rust policy/model/wire crates should forbid unsafe code. Platform/FFI crates keep unsafe operations in narrow modules with local safety rationale. C shims expose small opaque-handle/fixed-width interfaces; Rust owns high-level lifecycle and policy.

Assembly is appropriate for pixel/format/scaling/color hot paths, not protocol semantics, resource lifetime, WM/compositor policy, IPC semantics, KMS state, input routing, selections/clipboard, or config parsing.

Recommended platform dependencies include Linux DRM/KMS APIs, `libdrm`, `libinput`, udev access, Mesa/GBM/EGL where appropriate, `xcb-proto` XML for protocol reference/code generation, narrowly justified Rust crates, and retained native libraries.

Do not make Xorg/XLibre server source, wlroots, Weston/Mutter/KWin internals, Wayland as a required runtime protocol stack, or a large framework that hides display-server internals into core dependencies.

## 4. Build and migration boundaries

Cargo is the primary Rust inner-loop interface. Meson + Ninja remain authoritative for retained legacy C/C++ and native shims during migration.

- Keep the graphs independent: ordinary Cargo work must not build the whole Meson tree, and ordinary Meson configuration must not build every Rust test binary.
- A developer/checkpoint orchestration command may invoke both explicitly.
- Preserve configure/build/test health and `compile_commands.json` where practical.
- Generated code needs a deterministic generator and documented regeneration command.
- Do not refactor legacy C++ merely for elegance while migrating it. First preserve behavior, then replace it.

## 5. Testing and headless-first development

Tests are required for new behavior unless a concrete limitation is documented. Use the cheapest tier capable of disproving the change:

1. targeted compile/type checks;
2. unit/parser/pure-policy/fake-backend tests;
3. IPC/wire/component/cross-language fixture tests;
4. minimal-process/headless integration;
5. full software compatibility/sanitizer/install/VM acceptance;
6. explicitly authorized physical DRM/KMS acceptance.

New protocol, WM, IPC, compositor, render, and input behavior should be testable without real hardware where practical. Prefer synthetic clients, explicit IPC fixtures, and a headless `gwcomp` backend before DRM/KMS.

During the Rust transition, physical hardware is not part of the normal development loop. Hardware-capable commands must fail closed unless `GW_ALLOW_HARDWARE_TESTS=1` is set to exact value `1`; ordinary wrappers omit it. Do not acquire DRM master, stop Mike's desktop/session, or ask him to free the GPU for intermediate validation. Physical re-acceptance is a bounded final gate after software acceptance and explicit authorization, with explicit rollback/recovery notes for real DRM/KMS changes.

When porting behavior, do not migrate implementation and expected output together. Prove the fixture against the accepted legacy oracle first, then run the same fixture against the Rust replacement. Retire legacy code only after its compatibility gate passes.

Harness/parser/timeout/readiness/fixture defects are first-class bugs. Add focused regression coverage before rerunning broader or physical gates.

## 6. Compatibility and modern display features

Do not claim broad X11 compatibility without tests. For protocol compatibility, record supported request/reply/event behavior, motivating clients, proving tests, and intentionally unsupported behavior. Follow the compatibility tiers in `docs/GLASSWYRM_SPEC.md`.

HDR, VRR, and per-output scaling are core goals but must not destabilize the foundation. Milestone-15 feature work remains frozen until the Rust-first transition acceptance gate passes, except for correctness fixes.

Preserved architecture order:

1. protocol/server core and `libgwipc` contract;
2. headless `gwcomp` and minimal `gwm` policy;
3. software renderer and simple X clients;
4. DRM/KMS backend;
5. per-output metadata/scaling, then VRR, then HDR/color prototypes;
6. accelerated/fullscreen paths.

## 7. Security and runtime scope

Early Glasswyrm is local-only. Do not add TCP listening by default, setuid requirements, or claims of Wayland-like client isolation. Document current limitations accurately.

Keep protocol/resource/input/config/image/IPC parsing bounded and fail closed at malformed boundaries. Runtime authority stays explicit between `glasswyrmd`, `gwm`, and `gwcomp`.

## 8. Documentation and local plans

Update durable documentation when changing architecture, server/WM/compositor IPC, public tool behavior, protocol behavior, compatibility tiers, build/dependency assumptions, test workflow, Gentoo packaging, HDR/VRR/scaling policy, or security boundaries.

Use `docs/decisions/` for choices likely to evolve.

`Plans/` is local planning state:

- keep it untracked and visible in `git status`;
- do not add it to `.gitignore` or `.git/info/exclude`;
- never stage/commit a plan unless Mike explicitly requests that specific plan.

## 9. Gentoo packaging

Preserve a split runtime/package model rather than one monolith:

- `x11-base/glasswyrm` — metapackage/session bundle
- `x11-base/glasswyrmd` — protocol server
- `x11-wm/gwm` — WM policy
- `x11-base/gwcomp` — compositor/display authority
- `x11-apps/gw-tools` — developer/runtime tools
- `gui-libs/libgwipc` — shared IPC contracts
- `gui-libs/libgwproto` — protocol helpers when separately installed
- `gui-libs/libgwrender` — renderer helpers when separately installed

Split packages should reduce rebuild/install scope. Prefer shared release sources, shared `DISTDIR`, or an intentional local cache/mirror rather than redundant downloads. Pin live-ebuild commits for reproducible VM validation unless testing latest intentionally.

Maintain the local overlay under `packaging/gentoo/overlay/` once packaging work is active. It should include `profiles/repo_name`, package categories/metadata/ebuilds, and any experimental package.mask/package.use guidance needed by the split. A fresh Gentoo validation should exercise Portage dependency resolution, USE flags, build options, install paths, session/service files, and uninstall behavior rather than merely copying runtime binaries.

VM/package validation remains subject to the workspace VM/external-write rules. Do not invent a new VM, host share, network transport, privileged mount, or package-install authority in this file. When an explicitly authorized/designated VM is used, record the exact overlay/source revision and effective package graph.

For narrow updates, check pretend output before install. A `gwm`-only revision should not rebuild `gwcomp` or `glasswyrmd` unless a shared library/IPC ABI change requires it. Express shared ABI rebuild relationships explicitly rather than relying on drift.

## 10. Completion checks

Before claiming Glasswyrm work complete:

- relevant build/test tiers pass or exact blockers are reported;
- new behavior has focused tests or a documented limitation;
- architecture/process ownership and compatibility claims remain accurate;
- no unrelated formatting/user changes are included;
- docs are updated where contracts/workflow changed;
- physical-hardware claims are made only from explicitly authorized physical tests.

Git/commit/branch/push workflow comes exclusively from the root `AGENTS.md`; this file does not override it.
