# Milestone 12 source-layout audit

Status: accepted implementation layout; final VM acceptance complete.

Required baseline: `ae6b6c93a29a1fb985dcea8455650d15c0fec364`

## Guard and method

`tests/tools/source_layout_test.sh` classifies every C/C++ source and header
under `src/` against the required Milestone 11 baseline. It counts physical
lines including comments and blanks and enforces:

- 1,000 lines as the hard default for an unchanged file;
- 600 lines for a new or materially rewritten file;
- 500 lines for coordinators;
- 250 lines for `main.cpp` files;
- an ordinary-function target of 100 lines with review above 150 lines; and
- exact, non-stale entries in `docs/maintenance/source_size_allowlist.txt`.

The allowlist has no active entry. No Milestone 12 source file is allowlisted,
and the current guard run has no file-budget, coordinator, main, routing-shell,
or stale-allowlist failure.

## Milestone 12 decomposition

The implementation keeps the new responsibilities separated at their owning
boundaries:

| Responsibility | Primary implementation areas | Disposition |
| --- | --- | --- |
| Opt-in setup, registry, extension wire, and dispatch | `src/protocol/x11/`, `src/glasswyrmd/extension_*` | Registry and wire mechanics stay outside the core request switch. |
| Colormaps and extension resources | focused `src/glasswyrmd/*_store.cpp`, resource cleanup/invariant modules | One XID namespace with type-specific storage and cleanup. |
| BIG-REQUESTS framing | `src/protocol/x11/request.*`, client connection state | Incremental framing remains separate from request semantics. |
| MIT-SHM | `src/glasswyrmd/extensions/mit_shm.*`, `shm_store.cpp` | SysV ownership/mapping and request dispatch are bounded modules. |
| XFIXES, DAMAGE, and RANDR | focused extension dispatch files plus region, damage, and RANDR state stores | Geometry/state models are separate from wire request families. |
| RENDER and COMPOSITE | focused extension dispatch, picture/render primitives, and composite state | Scalar pixel semantics and resource lifetime are not embedded in the dispatcher. |
| EWMH and fullscreen policy | `ewmh*.cpp`, lifecycle projection/runtime, `src/wm/policy_engine.cpp` | X11 interpretation remains in the server and deterministic placement remains in GWM policy. |
| Eventfd CPU-buffer synchronization | published-buffer/content-presenter modules, GWIPC validation/wire, `src/gwcomp/buffer_readiness.cpp` | Producer readiness and consumer waiting remain distinct from rendering. |
| Renderer selection and reports | `src/render/`, `src/gwcomp/renderer_runtime.*` | Software and GLES implementations share a small renderer contract; EGL context, draw loop, and reporting are split. |
| Damage-aware scanout | `src/backends/drm/damage_copy.*`, presenter damage/report modules | Generation history and copy calculation stay below the presentation transaction. |
| Session and VM acceptance | `src/session/`, `tools/gw-vm.d/lib/milestone12.sh`, `tests/compat/m12/` | Runtime argv construction remains shell-free; test scripting is outside the runtime stack. |

## Reviewed function spans

The guard's advisory lexical scan currently reports nine spans above the
150-line review threshold. These are reviewed, not allowlist exceptions:

| Area | Approximate span | Review disposition |
| --- | ---: | --- |
| `CompositorPeer::submit` | 189 | Retains one atomic snapshot/buffer/cursor replay submission. Split when another retained surface class is added. |
| `ServerRuntime::service_input` | 164 | Keeps validation, routing, acknowledgement, and focus publication in one ordered synthetic-input transaction. |
| `ServerRuntime::service_integrated` | 218 | Coordinates peer readiness, input/session state, replay, and publication for one reactor cycle. |
| `dispatch_request` | 166 | Remains a flat routing shell; request semantics live in focused handler and extension modules. |
| `RuntimeBridge::service` | 177 | Preserves atomic evaluation of the two-peer connection and retry state machine. |
| `gwcomp` option parser | 151 | Parses one bounded CLI vocabulary; backend and renderer validation remain in helpers. |
| `PresentationTransaction::commit` | 277 | Owns the staged render/present/promote/rollback transaction; renderer and presenter mechanics are delegated. |
| `gwcomp::run` | 404 | Owns process lifetime, poll sources, backend/session wiring, and ordered shutdown while renderer and DRM policies remain separate. |
| `wm::evaluate` | 233 | Evaluates one immutable policy snapshot in deterministic state, stacking, focus, transient, and eligibility order. |

Spans between 101 and 150 lines remain advisory targets and do not weaken the
machine-enforced file budgets. Future changes to any reviewed coordinator must
re-run the guard and reconsider decomposition rather than adding an exception.

## Current proof boundary

The source-layout script passes at the accepted implementation snapshot with
nine review items and an empty active allowlist. This proves repository
structure, not runtime correctness; strict compiler, sanitizer, component,
historical regression, and clean Gentoo VM gates independently prove the
remaining acceptance boundaries.
