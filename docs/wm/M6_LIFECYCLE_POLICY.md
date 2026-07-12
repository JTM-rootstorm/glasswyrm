# Milestone 6 Lifecycle Policy

M6 extends the M5 deterministic policy service without moving X11 protocol
ownership into `gwm`. The accepted peer remains one same-UID
`ProtocolServer`, now requiring WindowLifecycle in addition to Snapshots and
WindowPolicy.

Every lifecycle evaluation is a complete `WindowPolicy` replacement containing
one fixed context and all top-level InputOutput candidates. Lifecycle upserts
add geometry and stack serials, sibling identity, and Above/Below intent. Zero
windows is a valid bootstrap.

`gwm` evaluates placement, visibility, stacking, focus, management state,
decoration eligibility, and fullscreen/direct-scanout eligibility. It returns
one state per candidate in deterministic visible stack order followed by hidden
window ID order. The acknowledgement contains the canonical policy hash.

Override-redirect windows remain unmanaged and preserve requested geometry.
Ordinary first placement retains the M5 cascade unless lifecycle geometry
serials provide a committed request. Unsupported or inconsistent sibling and
stack relationships reject the policy rather than silently importing legacy
X11 behavior.

`glasswyrmd`, not `gwm`, owns real X11 resources and commits evaluated state
only after the compositor accepts the translated metadata scene. Policy
rejection leaves committed server state unchanged. A later compositor
rejection requires replay of committed policy and scene state.

Unit and process tests cover deterministic policy output, complete lifecycle
upserts, correlated replies and hashes, coordinator rollback/replay phases, and
the M5 regression path. Full live-peer restart and golden M6 scenario fixtures
remain acceptance work and are not claimed complete here.

