# 0013: Keep Interactive Input in the Server and Display Authority in the Compositor

Status: Provisional; implementation and live Milestone 11 acceptance pending

## Context

Milestone 10 gave `gwcomp` direct DRM/KMS and VT authority while
`glasswyrmd` retained synthetic input and X11 event truth. A usable local
desktop needs real devices, keyboard state, cursors, selections, grabs, and
window-manager interactions without creating a second display or policy path.

## Decision

Real input is an opt-in `glasswyrmd` profile. It uses libinput's path backend
with an explicit startup allowlist and libxkbcommon with a fixed US `pc105`
profile. Both real and synthetic records enter the existing input router;
synthetic M8 acknowledgement semantics remain unchanged. `glasswyrmd` owns
X11 input, grab, cursor-resource, and selection state.

`gwm` publishes one capability-gated binding record and remains the authority
for focus, stacking, move, resize, and close policy. `glasswyrmd` recognizes
those bindings and sends geometry through the existing lifecycle transaction.

`gwcomp` remains the only display authority. It coordinates libinput
suspend/resume with VT transitions through GWIPC session-state messages and
composites one ARGB software cursor after ordinary surfaces. There is no
hardware cursor plane or second DRM owner.

`glasswyrm-session` is an unprivileged process orchestrator, not a seat broker.
The caller must already have access to the DRM node, VT, and named input
devices.

## Compatibility boundary

The intended M11 external-client claim is limited to xterm patch 410 under the
pinned core-font ASCII profile. Live trace, VM, and screenshot acceptance are
still required before that claim is marked validated. XKB extension support,
XIM/compose, arbitrary layouts, full grabs, themed cursors, clipboard
persistence, decorations, and multiple workspaces or outputs remain deferred.
