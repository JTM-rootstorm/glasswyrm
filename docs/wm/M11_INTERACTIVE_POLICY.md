# Milestone 11 Interactive Policy

When `InteractivePolicy` is negotiated, each complete GWM output snapshot
contains exactly one `PolicyBindingsUpsert`. The default binding record is:

```text
move=Mod1+Button1 resize=Mod1+Button3 close=Mod1+F4
minimum-size=96x64 raise-on-focus=true consume-bindings=true
```

The record is validated for known modifier bits, buttons 1-9, a nonzero close
keysym, bounded nonzero minimum dimensions, booleans, and zeroed reserved
fields. It participates in the capability-specific v2 policy hash. Peers that
do not negotiate the capability retain the v1 snapshot and hash.

A matching press on a managed, visible direct-root InputOutput window begins
one internal move or bottom-right-resize interaction. Initial pointer and
committed geometry are retained; later motion is coalesced while at most one
lifecycle transaction is in flight. Move preserves size. Resize preserves
position and clamps to the published minimum. Geometry becomes server truth
only after GWM policy and compositor presentation accept the normal lifecycle
transaction. Releasing the initiating button completes the interaction.

Alt+F4 operates on the focused managed top-level. If `WM_PROTOCOLS` contains
`WM_DELETE_WINDOW`, the server sends the standard format-32 ClientMessage. The
M11 fallback destroys only that top-level through the coordinated policy path;
it is not ICCCM `XKillClient` behavior.

Target destruction, loss of visibility/management, peer disconnect, VT
suspension, or input loss aborts cleanly at the last committed geometry.
Decorations, edge hit testing, multiple interactions, and workspaces are
deferred.
