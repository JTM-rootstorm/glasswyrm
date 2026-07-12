# 0008: Route Top-Level Lifecycle Through Three Processes

Status: Accepted

## Context

Milestones 2, 4, and 5 established separate X11 resource, compositor, and
window-policy models. Connecting those models must not make `gwm` an X11
client or turn `gwcomp` into a legacy redirected-pixmap compositor.

## Decision

In explicit integrated mode, `glasswyrmd` connects as a GWIPC
`ProtocolServer` to the `WindowManager` listener owned by `gwm` and the
`Compositor` listener owned by `gwcomp`. There is no `gwm` to `gwcomp`
connection. `glasswyrmd` translates validated policy output into compositor
metadata.

Each policy operation uses a complete replacement snapshot. Only one lifecycle
transaction is in flight; later requests from its X11 client remain behind a
per-client dispatch barrier while other clients may continue safe work. Real
X11 resource state is committed only after both policy and compositor
acknowledgements are accepted.

The compositor projection uses metadata-only surfaces paired with explicit
surface-policy records. It imports no client buffer and produces no raster
frame or PPM for this path.

Startup synchronizes an empty policy snapshot and an output-only compositor
snapshot before the X11 listener is opened. Connections use bounded retry and
deadlines. The coordinator and peer APIs retain full snapshots for rollback
and replay. Unit tests cover rollback and reconnect phases, and the fixed M6
acceptance harness proves live-client survival across peer restarts.

When IPC support is disabled, `glasswyrmd` retains its standalone behavior.
There is no hidden built-in window-management policy.

## Consequences

- Protocol, policy, and display authority remain explicit process boundaries.
- Full snapshots favor deterministic recovery over incremental efficiency.
- Mapping currently projects window metadata only; drawing and input remain
  unsupported.
- The local same-UID trust model and unauthenticated X11 listener remain
  documented early-development limitations.
