# Glasswyrm Agent Instructions

The workspace `../CLAUDE.md` applies and owns coordination, authorization,
external/VM effects, Git/commit/push policy, licensing, and security-review
recording. This file adds only Glasswyrm technical doctrine.

Glasswyrm is a from-scratch, local-first X11-compatible display stack for
Gentoo Linux on x86_64. It is not a fork of Xorg, XLibre, Xwayland, wlroots,
Weston, Mutter, KWin, or another display/compositor stack.

## 1. Authority and architecture

Before implementation read, in order:

1. `docs/GLASSWYRM_SPEC.md`;
2. relevant committed decisions/designs under `docs/`; and
3. the affected source and tests.

A clearly newer committed decision may supersede older spec text. Otherwise
surface contradictions; update an in-scope contract or stop rather than
silently choosing between spec and code.

Glasswyrm is X11-compatible where useful, not Xorg-compatible by default.
Keep the authority split explicit:

- `glasswyrmd` owns X11 protocol and resource truth;
- `gwm` owns window-management policy; and
- `gwcomp` owns final composition, presentation, and output authority.

`gwcomp` must not become an ordinary X11 client or a legacy external compositor
that merely assembles redirected pixmaps.
Do not import legacy behavior without a concrete compatibility target and test.
Favor explicit compatibility tiers, clean internal boundaries, and
headless-testable policy. The primary runtime targets local Gentoo sessions;
real output uses DRM/KMS and real input uses libinput.

## 2. Languages, dependencies, and builds

New runtime code is Rust-first. Allowed exceptions are C for bounded
platform/ABI shims where it is simpler and more auditable; C++ only for
retained migration code, temporary legacy/Rust comparison adapters, or
unavoidable dependencies; and x86_64 assembly only for isolated, profiled hot
paths with a correct Rust/C reference. Do not introduce another runtime
language without explicit approval. Python and shell may be used for justified
build/test/generator helpers, not runtime components.

Policy, model, and wire crates should forbid unsafe code. Platform/FFI crates isolate
unsafe operations in narrow modules with local safety rationale. C shims expose
small opaque-handle/fixed-width interfaces while Rust owns high-level lifecycle
and policy. Assembly is appropriate for pixel, format, scaling, and color hot paths,
but never protocol semantics, resource lifetime, WM/compositor policy, IPC
semantics, KMS state, input routing, selections/clipboard, or config parsing.

Preferred platform inputs include Linux DRM/KMS, `libdrm`, `libinput`, udev,
Mesa/GBM/EGL as needed, `xcb-proto` XML for protocol reference/generation, and
narrowly justified Rust crates and retained native libraries. Do not make Xorg/XLibre server code,
wlroots, Weston/Mutter/KWin internals, Wayland as a required runtime stack, or a
large framework hiding display-server internals into core dependencies.

Cargo is the primary Rust inner loop; Meson/Ninja remain authoritative for
retained legacy C/C++ and native shims during migration. Keep the graphs
independent: ordinary Cargo work must not build the whole Meson tree, and
ordinary Meson configuration must not build every Rust test binary. Explicit
checkpoint orchestration may invoke both. Preserve
configure/build/test health and `compile_commands.json` where practical.
Generated code needs a deterministic generator and documented regeneration
command. Preserve legacy behavior first; do not refactor soon-to-be-retired C++
merely for elegance.

## 3. Validation and migration gates

Tests are required for new behavior unless a concrete limitation is recorded.
Use the cheapest tier capable of disproving a change:

1. targeted compile/type checks;
2. unit, parser, pure-policy, and fake-backend tests;
3. wire, IPC, component, fixture, and cross-language tests;
4. minimal-process/headless integration;
5. full software compatibility, sanitizer, install, and VM gates; and
6. explicitly authorized physical DRM/KMS acceptance.

Protocol, WM, IPC, compositor, render, and input behavior should be testable
without real hardware where practical. Prefer synthetic clients, explicit IPC
fixtures, and a headless `gwcomp` backend before DRM/KMS. During the Rust transition,
physical hardware is not part of the normal development loop. Hardware-capable commands must fail
closed unless `GW_ALLOW_HARDWARE_TESTS=1` is set to exact value `1`. Ordinary wrappers omit it.
Do not acquire DRM master, stop Mike's desktop/session, or request GPU release
for intermediate checks. Physical re-acceptance is a bounded final gate after
software acceptance and explicit authorization, with explicit rollback/recovery
notes for real DRM/KMS changes.

For migrated behavior, first prove a fixture against the accepted legacy
oracle, then run that same fixture against Rust; do not migrate implementation
and expectation together. Retire legacy code only after its compatibility gate
passes. Harness, parser, timeout, readiness, and fixture defects are first-class bugs:
add focused regression coverage before broader or physical reruns.

Do not claim broad X11 compatibility without recording supported request,
reply, and event behavior, motivating clients, tests, and deliberate gaps.
Follow the compatibility tiers in `docs/GLASSWYRM_SPEC.md`.
HDR, VRR, and per-output scaling remain core goals but must not destabilize the
foundation. Milestone 15 stays frozen until Rust-transition acceptance except
for correctness fixes. Preserve this implementation order: protocol/server and
`libgwipc`; headless compositor and minimal WM; software renderer/simple X clients;
DRM/KMS; output scaling/metadata; VRR; HDR/color; acceleration/fullscreen.

## 4. Runtime, documentation, and local plans

Early Glasswyrm is local-only. Do not add TCP listening by default, setuid
requirements, or Wayland-like isolation claims. Keep protocol, resource, input,
configuration, image, and IPC parsing bounded and fail closed at malformed
boundaries; preserve
explicit authority across all three processes. Document current limitations
accurately.

Update durable docs with changes to architecture, server/WM/compositor IPC,
public tool behavior, protocol behavior, compatibility tiers, build/dependency
assumptions, test workflow, Gentoo packaging, HDR/VRR/scaling policy, or
security boundaries. Put evolving decisions in `docs/decisions/`.

`Plans/` is untracked local state: keep it visible in `git status`, never add it
to `.gitignore` or `.git/info/exclude`, and never stage or commit a plan unless
Mike requests that specific file.

## 5. Gentoo packaging

Preserve the split runtime/package model:

- `x11-base/glasswyrm` - metapackage/session bundle;
- `x11-base/glasswyrmd` - protocol server;
- `x11-wm/gwm` - WM policy;
- `x11-base/gwcomp` - compositor/display authority;
- `x11-apps/gw-tools` - developer/runtime tools;
- `gui-libs/libgwipc` - shared IPC contracts;
- `gui-libs/libgwproto` - protocol helpers when separately installed; and
- `gui-libs/libgwrender` - renderer helpers when separately installed.

The split should reduce rebuild/install scope. Prefer shared release sources,
`DISTDIR`, or an intentional cache/mirror over duplicate downloads. Pin live
ebuild commits for reproducible VM validation unless deliberately testing latest.
Once packaging work is active, maintain the overlay under
`packaging/gentoo/overlay/` with `profiles/repo_name`, package categories,
metadata, ebuilds, and needed experimental `package.mask`/`package.use`
guidance.

Fresh Gentoo validation should exercise Portage resolution, USE/build options,
install paths, session/service files, and uninstall behavior, not merely copy
binaries. VM/package work remains under root workspace authority; this file
does not invent a new VM, host share, network transport, privileged mount, or
package-install authority. For an explicitly authorized/designated VM, record
exact source/overlay revisions and the effective package graph. For narrow
updates, inspect pretend output before install. A `gwm`-only change should not rebuild
`gwcomp` or `glasswyrmd` unless a shared library/IPC ABI changed; encode
shared-ABI rebuild relationships explicitly rather than relying on drift.

## 6. Completion

Before claiming completion, ensure relevant tiers pass or report exact
blockers; new behavior has tests or a recorded limitation; architecture,
ownership, and compatibility claims remain accurate; docs match changed
contracts/workflows; unrelated formatting/user changes are excluded; and physical claims
come only from explicitly authorized physical evidence.

Git/commit/branch/push workflow comes exclusively from the root `CLAUDE.md`;
this file does not override it.
