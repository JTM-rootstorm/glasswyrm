# Milestone 13 multi-output policy

Milestone 13 retains one global workspace and one global stacking and focus
order. `gwm` receives the complete enabled output map and remains the sole
owner of window assignment, placement, maximize, fullscreen, focus, and stack
policy. Coordinates and window sizes are global logical units; crossing an
output scale boundary never resizes a normal window.

## Assignment and placement

New managed windows use a valid preferred output or the primary output and
cascade independently within that output. Output assignment follows this
deterministic order:

1. a transient inherits its parent's output;
2. fullscreen or maximized state keeps the previous valid output when possible;
3. otherwise choose the enabled output with greatest intersection area;
4. ties prefer the previous output, then preferred hint, then primary output,
   then the lowest stable output ID.

Override-redirect windows use the same intersection assignment but retain
their unrestricted requested geometry. A hidden or offscreen window may have
no geometric membership while retaining an assigned primary output for policy
and future movement.

Managed windows retain at least one logical pixel within an enabled output,
but are not clamped wholly onto one output. Spanning windows are intentional.
Fullscreen and maximize use the assigned output's work rectangle; leaving
fullscreen restores global logical normal geometry and re-evaluates
assignment.

## Policy snapshots

GWIPC API 0.8 adds one `PolicyOutputUpsert` for each output and optional
`PolicyWindowOutputHint` records. The historical `PolicyContextUpsert` remains:
its output is the primary output and its M13 work rectangle is the root
bounding box. Per-output records carry actual work areas.

M13 uses policy hash profile v3. Historical v1 and v2 bytes do not change.
Reconnect replays the complete snapshot and must reproduce the same v3 hash.
During an output configuration, policy results are staged and do not become
server-visible until the compositor accepts the corresponding complete scene.
If that stage rejects, the old policy snapshot and hash are restored.

Surface membership is geometric and ordered by logical y, logical x, then
output ID. A surface's preferred rational scale is the scale of its assigned
primary output. Cursor primary selection uses the output containing its
hotspot, with deterministic tie handling at output edges.

Per-output workspaces, per-output stack bands, auto-placement beyond the fixed
initial policy, and persistent output assignment are unsupported.
