# Milestone 11 Source Layout Audit

Status: final Milestone 11 source-layout review passes.

The M10 exception for `src/ipc/connection.cpp` has been removed. The public C
ABI/lifecycle shell is now 70 lines, with private work divided into handshake,
transport, polling, queues, validation, and correlation modules. Current IPC
implementation files are each below 600 lines; the largest is
`connection_transport.cpp` at 457 lines and correlation is 414 lines.

`tests/tools/source_layout_test.sh` now classifies material changes against the
M10 merge baseline `9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0`. It preserves
the 600-line new/material rewrite limit, 500-line coordinator limit, 250-line
`main.cpp` limit, 100-line function target, and review above 150 lines.
`docs/maintenance/source_size_allowlist.txt` contains no active exception.

M11 responsibilities are separated across `src/input` (device access,
libinput conversion, xkb state, repeat, cursor model), focused server request
handlers and stores (cursor, grabs, selections, keyboard control),
`src/wm/interactive_policy.*`, `src/gwcomp/session_state_coordinator.*`, and
the standalone session launcher. No new M11 source file is allowlisted.

The final `tests/tools/source_layout_test.sh` run passes with eight advisory
function-review findings and no file-budget or allowlist failure. Each advisory
was reviewed as follows:

| Function | Approximate span | Review disposition |
| --- | ---: | --- |
| `CompositorPeer::submit` | 186 | Keeps snapshot validation, retained-cursor replay, and one atomic compositor submission together. Split if another retained surface class is added. |
| `ServerRuntime::service_input` | 164 | Owns the ordered synthetic-input transaction: provider lifecycle, validation, routing, acknowledgement, and focus publication. Real input remains in separate modules. |
| `ServerRuntime::service_integrated` | 218 | Is the top-level integrated-cycle coordinator for peer readiness, real-input suspension/resume, replay, and publishing. Its branches share cycle state; split when another input backend or peer role is introduced. |
| `dispatch_request` | 155 | Is a deliberately flat opcode-to-handler routing shell with behavior implemented in focused request-handler modules. |
| `RuntimeBridge::service` | 177 | Keeps the peer-pair connection/retry state machine in one transition function so failure and readiness are evaluated atomically. |
| `PresentationTransaction::commit` | 306 | Preserves the staged frame transaction and rollback boundary across scene, attachment, presentation, and evidence publication. Existing helpers hold independent calculations. |
| `gwcomp::run` | 394 | Is process-lifetime orchestration for backend creation, IPC, polling, and orderly shutdown; rendering and DRM policy remain outside it. |
| `evaluate` | 202 | Evaluates one immutable WM policy snapshot in deterministic order, including placement, stacking, focus, and transient constraints. |

Most of the M11 VM helper is one quoted, self-contained guest program plus
host-side screenshot and evidence coordination. Extracting that program would
add a copied artifact and a host-to-guest version/argument boundary while the
CLI tests intentionally inspect the exact embedded program. The diagnostic
`milestone11-interactive-rerun` therefore reuses the same guest program through
an explicit mode and a marker retaining full-build provenance. That mode may
reconfigure and incrementally compile only the existing runtime tree; it cannot
accidentally become the full acceptance path. Revisit physical extraction if
another independent scenario needs only a smaller portion of the interactive
runtime.
