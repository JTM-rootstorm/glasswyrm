# AGENTS.md

This repository is the source of truth for the Glasswyrm project.

Glasswyrm is a from-scratch, local-first, X11-compatible display stack for modern Linux/Gentoo. Its runtime is Rust-first, with bounded C and selective x86_64 assembly where those are justified. C++ is migration-only legacy code. Glasswyrm is not a fork of Xorg, XLibre, Xwayland, wlroots, Weston, Mutter, KWin, or any other display server/compositor stack.

## Read first

Before implementing, read:

1. `docs/GLASSWYRM_SPEC.md`
2. `AGENTS.md`
3. Any relevant design notes under `docs/`
4. Existing code in the subsystem being changed

If the spec and code disagree, prefer the current code only when it clearly reflects a newer committed design decision. Otherwise update the spec or ask for clarification before building on a contradiction.

## Project doctrine

Glasswyrm is X11-compatible where useful, not Xorg-compatible by default.

Current architecture is a traditional X11-shaped split with Glasswyrm-owned
processes: `glasswyrmd` for protocol truth, `gwm` for window-management policy
truth, and `gwcomp` for final display authority. Keep those boundaries
explicit. `gwcomp` must not become an ordinary X11 client or a legacy external
compositor that merely assembles redirected pixmaps.

Do not drag legacy behavior into the project without a specific compatibility target and test. Favor clean internal architecture, headless testing, and explicit compatibility tiers.

Primary target:

- Gentoo Linux
- x86_64
- Local desktop sessions
- DRM/KMS for real display output
- libinput for real input

## Language rules

Allowed implementation languages:

- Rust (default)
- C (bounded platform and ABI shims)
- C++ (existing migration-only code and temporary comparison adapters)
- x86_64 assembly (profiled optional hot paths)

Do not introduce Go, Zig, Java, C#, Python runtime components, or other implementation languages unless explicitly instructed by the user.

### Rust rules

Use Rust by default for new runtime code, including server and resource state,
X11 parsing and dispatch, window-manager policy, IPC codecs and contracts,
compositor and output state, display policy, event and input orchestration,
renderer orchestration, configuration, command-line tools, process supervision,
and test infrastructure.

Prefer:

- explicit ownership and typed IDs
- small state machines and enums instead of magic constants
- `Result`-based error propagation
- deterministic pure functions for policy decisions
- narrow, documented `unsafe` boundaries
- dependency-light crates with one clear responsibility

Pure policy and wire crates should forbid unsafe code. Platform/FFI crates must
keep unsafe operations in explicit blocks and document the safety invariants.

Python, shell, or similar scripting is acceptable for build helpers,
generators, and tests when justified, but the Glasswyrm runtime stack itself
should remain Rust with bounded C/assembly and migration-only C++.

### C rules

Use C only for bounded platform or ABI shims where C is simpler and more
auditable than reproducing the system boundary directly in Rust. Candidate
boundaries include selected DRM/KMS, GBM/EGL/GLES, libinput, udev, generated
system glue, and temporary stable C ABI exports for legacy code.

C shims should expose small opaque-handle APIs and plain fixed-width data. Rust
owns high-level lifecycle and policy.

### C++ rules

C++ is allowed only for existing implementation that has not yet migrated,
temporary adapters that compare legacy and Rust behavior, or a dependency with
no practical C or Rust boundary. Do not add new architecture-heavy production
C++ during the transition. Preserve explicit ownership and narrow interfaces
while migrating; do not refactor legacy code merely to make it more elegant.

### Assembly rules

Assembly is allowed only when it is isolated, tested, and has a correct Rust or
C reference implementation.

Do not implement a new feature only in assembly. First implement a Rust or C
reference path, then add the assembly optimization behind runtime CPU feature
detection and build flags.

Assembly is appropriate for:

- pixel blending
- format conversion
- scaling blits
- color conversion
- carefully isolated hot paths

Assembly is not appropriate for:

- protocol semantics
- window/resource lifetime
- window manager policy
- compositor policy
- IPC contract semantics
- KMS state management
- input routing
- selections/clipboard
- configuration parsing

## Dependency rules

Allowed/recommended dependencies include:

- Linux DRM/KMS APIs
- `libdrm`
- `libinput`
- `libudev` or equivalent udev access
- Mesa/GBM/EGL where appropriate
- Vulkan later if explicitly useful
- `xcb-proto` XML for protocol reference/code generation
- the Rust standard library and narrowly justified Rust crates
- standard C/C++ libraries required by retained native code
- test libraries when justified

Do not depend on:

- Xorg server source
- XLibre server source
- wlroots
- Weston/Mutter/KWin internals
- Wayland as a required runtime protocol stack
- large frameworks that hide display-server internals

## Build expectations

Cargo is the primary Rust inner-loop build and test interface. Meson + Ninja
remain authoritative for the legacy C/C++ graph and retained native shims
during the transition. Keep the graphs independent: ordinary `cargo check`
must not build the whole Meson tree, and ordinary Meson configuration must not
build every Rust test binary. A developer orchestration command may invoke
both explicitly for checkpoint and acceptance suites.

Every meaningful implementation should preserve:

- configure success
- build success
- test execution
- `compile_commands.json` generation where possible

Do not add generated code without also documenting the generator and regeneration command.

## Testing rules

Tests are mandatory for new behavior unless there is a documented reason they are not yet possible.

Preferred test order:

1. Tier 0: targeted `cargo check` or compile/type feedback
2. Tier 1: unit, parser, pure-policy, and fake-backend tests
3. Tier 2: IPC, wire, component, fixture, and cross-language contract tests
4. Tier 3: minimal-process and headless integration tests
5. Tier 4: full software acceptance, compatibility, sanitizer, install, and VM tests
6. Tier 5: explicitly authorized physical DRM/KMS acceptance

Run the cheapest tier capable of disproving a change first. A harness, parser,
timeout, readiness, fixture, or expected-value defect is a first-class bug:
add a focused harness regression, run it alone, then run the smallest affected
subsystem and software checkpoint. Do not repeat physical validation merely
because harness interpretation changed.

Do not migrate implementation and expected behavior together. First prove the
ported test against the legacy oracle, preserve the accepted fixture, then run
the same test against the Rust replacement. Retire legacy code only after that
replacement passes its compatibility gate.

Do not make real hardware access required for normal development tests.

Before completing a task, run the relevant build and test commands. If commands cannot be run in the environment, state exactly what could not be run and why.

## Headless-first rule

New protocol, window-manager, IPC, compositor, render, and input behavior should
be testable without real hardware whenever possible.

Prefer synthetic clients, a headless `gwcomp` backend, and explicit IPC fixtures
before touching DRM/KMS. Real hardware work must include rollback/recovery notes when appropriate.

During the Rust transition, physical hardware execution is not part of the
development loop. Hardware-capable commands must fail closed unless
`GW_ALLOW_HARDWARE_TESTS=1` is set to the exact value `1`, and ordinary test
wrappers must omit it.
Do not acquire DRM master, stop Mike's desktop/session, or ask him to free the
GPU for intermediate migration validation. Use the Glasswyrm VM for applicable
software, packaging, and non-real-hardware gates. Physical M14 re-acceptance is
a bounded final stage after Tier 4 is green and Mike explicitly authorizes it.

## Commit workflow

Commit often. Multiple commits per implementation are encouraged.

Rules:

- Make small, coherent commits.
- Split commits by subsystem when practical.
- If a task touches unrelated areas, use separate commits.
- Keep build fixes separate from feature commits when practical.
- Keep documentation updates near the code they explain.
- Co-author every AI-assisted commit so it shows both Mike and the assistant as authors.
- Use Mike's configured Git identity as the primary author unless he explicitly asks otherwise.
- Add `Co-authored-by: Codex <codex@openai.com>` to every Codex-assisted commit unless Mike explicitly requests a different assistant identity.
- Do not squash away useful history unless explicitly instructed.
- Do not rewrite history unless explicitly instructed.
- Do not force-push unless explicitly instructed.
- Push in bulk only after the task is complete and validated.

Commit message format:

```text
area: short imperative summary

Optional body explaining why and how.
```

Examples:

```text
protocol: add setup handshake parser
core: add resource table ownership checks
wm: add initial focus policy
ipc: define surface metadata messages
compositor: add headless framebuffer target
render: add ARGB over XRGB reference blend path
docs: record initial compatibility tiers
```

## Branch and push policy

Unless the user gives different instructions:

- Work on a task branch.
- Commit locally throughout the task.
- Push all task commits together only once the task is complete.
- Do not push broken intermediate states.
- Do not commit directly to `main` unless explicitly instructed.

If operating through a tool that cannot create local commits, produce patch files or clearly describe the intended commit split.

## Documentation policy

Update documentation when changing:

- architecture
- server/WM/compositor IPC contracts
- public tool behavior
- protocol behavior
- compatibility tiers
- build options
- dependencies
- test workflow
- Gentoo packaging assumptions
- HDR/VRR/scaling policy

Prefer design notes under `docs/decisions/` for choices that could plausibly change later.

## Local plans

Keep `Plans/` and everything under it untracked. Do not add `Plans/` to
`.gitignore`, `.git/info/exclude`, or any other Git exclusion mechanism; it
should remain visible in `git status`. Never stage or commit its contents unless
Mike explicitly requests that for a specific plan.

## Compatibility policy

Do not claim broad X11 compatibility without tests.

When implementing protocol features, record:

- which request/reply/event behavior is supported
- which clients motivated the support
- which tests prove it
- which behavior is intentionally unsupported

Compatibility targets should progress by tiers described in `docs/GLASSWYRM_SPEC.md`.

## Modern display feature policy

HDR, VRR, and per-output scaling are core goals, but they should not destabilize the foundation.

Milestone 15 feature work is frozen until the Rust-first transition acceptance
gate passes. Critical correctness fixes are allowed, but new HDR/color
architecture must not accumulate in the C++ tree merely to be migrated later.

The preserved architectural order remains:

1. core protocol/server
2. `libgwipc` contract skeleton
3. headless `gwcomp` compositor
4. minimal `gwm` placement/focus policy
5. software renderer
6. simple X clients
7. DRM/KMS backend
8. per-output metadata/scaling prototypes
9. VRR policy prototypes
10. HDR/color metadata prototypes
11. accelerated and fullscreen paths

Do not start with HDR or VRR before the server, window manager, compositor, and IPC foundation can be tested.

## Security policy

Early Glasswyrm is local-only.

Do not add TCP listening by default. Do not add setuid requirements. Do not claim Wayland-like client isolation. Document security limitations honestly.

## Gentoo packaging and VM validation

When touching packaging, preserve both the component split and the fresh-VM test
path. The expected runtime split is:

```text
x11-base/glasswyrm       # metapackage or session bundle
x11-base/glasswyrmd      # protocol/server process
x11-wm/gwm               # window-manager policy process
x11-base/gwcomp          # compositor/display authority process
x11-apps/gw-tools        # developer and runtime tools
gui-libs/libgwipc        # shared IPC contracts
gui-libs/libgwproto      # protocol helpers, if installed separately
gui-libs/libgwrender     # renderer helpers, if installed separately
```

Split packages should reduce rebuild and install scope. They do not, by
themselves, guarantee that Portage fetches less source. Prefer a shared release
tarball, shared `DISTDIR`, or an intentional local git cache/mirror for multiple
ebuilds that consume the same upstream tree. Live ebuilds should pin a commit
for reproducible VM tests unless the test is explicitly about latest `main`.

Maintain a local Gentoo overlay under `packaging/gentoo/overlay/` once packaging
begins. The overlay should be usable by a fresh Gentoo VM without editing the
upstream checkout. At minimum it should contain `profiles/repo_name`, package
categories, ebuilds, metadata, and any package.mask/package.use guidance needed
for experimental features.

For VM validation, Codex should provide the VM with the overlay through a shared
folder, tarball, rsync, or scp, then register it through repos.conf. A typical
manual shape is:

```sh
mkdir -p /etc/portage/repos.conf
cat >/etc/portage/repos.conf/glasswyrm-local.conf <<'EOF'
[glasswyrm-local]
location = /mnt/shared/glasswyrm-overlay
masters = gentoo
auto-sync = no
EOF
emerge --metadata
emerge --pretend --verbose --tree x11-base/glasswyrm
emerge -av x11-base/glasswyrm
```

Shared directories are useful for the overlay, distfiles, logs, and binary
packages. Do not use copied runtime artifacts as the only packaging test. The
fresh VM should exercise Portage dependency resolution, USE flags, Meson options,
install paths, service/session files, and uninstall behavior.

When testing narrow updates, verify the pretend output before emerging. A `gwm`
revision bump should not rebuild `gwcomp` or `glasswyrmd` unless a shared library
or IPC ABI change requires it. If `libgwipc` changes ABI, prefer explicit
same-version dependencies or subslot-driven rebuilds rather than silent drift.

## Code review checklist

Before considering a task complete, verify:

- The project still builds.
- Relevant tests pass.
- New behavior is tested or the missing test is justified.
- Logs/errors are useful enough to debug failures.
- No unrelated formatting churn is included.
- Documentation is updated where needed.
- The commit split is coherent.
- The final branch state is ready to push in bulk.

## When unsure

Prefer:

- smaller changes
- headless tests
- explicit TODOs with context
- documenting the decision
- preserving rollback ability

Avoid:

- broad rewrites
- silent behavior changes
- untested compatibility claims
- adding dependencies casually
- hiding uncertainty in code comments or commit messages
