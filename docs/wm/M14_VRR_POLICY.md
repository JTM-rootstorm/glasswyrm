# Milestone 14 VRR window policy

`gwm` owns whether a window is a suitable VRR candidate. It does not inspect
surfaces, mutate KMS, or decide effective state. The compositor remains free
to reject a selected candidate for presentation reasons.

## Inputs and results

An M14 policy snapshot adds one output VRR input per output and one window VRR
input per managed, non-override-redirect policy window. Output inputs carry the
requested mode plus capability and controllability facts. Window inputs carry
the application preference and the exact ordered output-membership set.

The reply contains one result for every input output and window, at most one
selected window per output, the base policy hash, and deterministic v4 hash.
Historical v1-v3 hashes and peers are unchanged. The server separately emits a
safe unmanaged compositor VRR record for override-redirect nonmetadata
surfaces; such windows are never GWM candidates.

## Common eligibility

A candidate window must be managed, visible, focused, assigned exclusively to
one enabled output, and not opted out with preference `Disable`. A spanning or
invalid membership is rejected explicitly. Selection iterates deterministic
policy/window order and chooses at most one eligible focused window for each
output.

Modes refine that common rule:

- `off` selects no candidate and requests disabled state;
- `fullscreen` additionally requires applied fullscreen or the exact
  borderless classification;
- `focused` allows an ordinary focused window;
- `app-requested` additionally requires preference `Prefer`;
- `always-eligible` requires no candidate and intentionally ignores window
  opt-out as an administrator override.

`Default` and `Allow` are eligible in Fullscreen and Focused modes but do not
request AppRequested mode. `Prefer` is eligible in every candidate mode.

## Borderless fullscreen

Borderless fullscreen is true only for a visible, focused, managed direct-root
InputOutput window that is not override-redirect or minimized, has no border,
does not require decorations, exactly covers its assigned output's complete
logical rectangle, has membership containing exactly that output, and is not
a transient whose parent is on another output. Covering only the work area is
not sufficient when it differs from the output rectangle.

## Transactions and reconnect

Focus, map, unmap, geometry, fullscreen, borderless, preference, destruction,
and output-policy changes reuse the ordinary lifecycle or output-control
transaction. Proposed policy results remain staged until the compositor
accepts the corresponding scene. Any wire or semantic rejection restores the
exact pre-transaction VRR cache before lifecycle cleanup. GWM reconnect
replays the committed snapshot and must reproduce the v4 hash.
