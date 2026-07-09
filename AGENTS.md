# AGENTS.md

This repository is the source of truth for the Glasswyrm project.

Glasswyrm is a from-scratch, local-first, X11-compatible display stack for modern Linux/Gentoo. It is intended to be implemented in C, C++, and selective x86_64 assembly. It is not a fork of Xorg, XLibre, Xwayland, wlroots, Weston, Mutter, KWin, or any other display server/compositor stack.

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

- C
- C++
- x86_64 assembly

Do not introduce Rust, Go, Zig, Java, C#, Python runtime components, or other implementation languages unless explicitly instructed by the user.

Python, shell, or similar scripting is acceptable for build helpers, generators, and tests when justified, but the Glasswyrm runtime stack itself should remain C/C++/assembly.

### C rules

Use C for low-level platform boundaries, DRM/KMS, libinput, udev, C ABI boundaries, and small protocol/platform helpers.

### C++ rules

Use C++ for architecture-heavy code: server state, resources, windows, window
manager policy, IPC contracts, compositor scene graph, output policy, event
routing, and renderer abstraction.

Prefer:

- RAII for resource lifetime
- explicit ownership
- small interfaces
- fixed-width integer types for protocol data
- simple state machines

Avoid:

- framework-heavy designs
- inheritance forests
- exceptions across subsystem boundaries
- hidden global mutable state
- unrelated formatting churn

### Assembly rules

Assembly is allowed only when it is isolated, tested, and has a C/C++ fallback.

Do not implement a new feature only in assembly. First implement a reference C/C++ path, then add the assembly optimization behind runtime CPU feature detection and build flags.

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
- standard C/C++ libraries
- test libraries when justified

Do not depend on:

- Xorg server source
- XLibre server source
- wlroots
- Weston/Mutter/KWin internals
- Wayland as a required runtime protocol stack
- large frameworks that hide display-server internals

## Build expectations

Preferred build system: Meson + Ninja.

Every meaningful implementation should preserve:

- configure success
- build success
- test execution
- `compile_commands.json` generation where possible

Do not add generated code without also documenting the generator and regeneration command.

## Testing rules

Tests are mandatory for new behavior unless there is a documented reason they are not yet possible.

Preferred test order:

1. Unit tests
2. Protocol parser tests
3. Window-manager policy tests
4. IPC contract and metadata round-trip tests
5. Headless integration tests
6. Pixel/golden tests
7. Fuzz or malformed-input tests where appropriate
8. Real DRM/KMS tests only as explicit hardware tests

Do not make real hardware access required for normal development tests.

Before completing a task, run the relevant build and test commands. If commands cannot be run in the environment, state exactly what could not be run and why.

## Headless-first rule

New protocol, window-manager, IPC, compositor, render, and input behavior should
be testable without real hardware whenever possible.

Prefer synthetic clients, a headless `gwcomp` backend, and explicit IPC fixtures
before touching DRM/KMS. Real hardware work must include rollback/recovery notes when appropriate.

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
- Add a `Co-authored-by:` trailer for the assistant identity on every AI-assisted commit; if no assistant identity is configured, ask Mike for the preferred co-author name and email before committing.
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

Recommended order:

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
