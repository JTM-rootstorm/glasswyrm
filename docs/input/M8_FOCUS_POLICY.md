# Milestone 8 Focus Policy

Button 1 press may request click focus when the pointer target is a viewable,
policy-visible, managed direct-root InputOutput window that is neither
cleanup-pending nor override-redirect and is not already focused. Root and
ineligible windows do not request focus.

`glasswyrmd` first enqueues ButtonPress and updates button state. It then
allocates a lifecycle/focus serial, proposes a full snapshot through the
existing FIFO, and pauses only later synthetic-input records. `gwm` remains
focus policy truth: it selects the greatest valid focus serial. On acceptance,
`glasswyrmd` projects the committed focus metadata to `gwcomp`, emits FocusOut
then FocusIn, acknowledges the click, and resumes input. This ordering ensures
a subsequently queued key routes to the new focus.

If policy selects another focus, the server rolls policy back to the committed
snapshot and reports `FocusRejected` after rollback. A target that disappears
or becomes ineligible before submission reports `FocusUnchanged`. An
unprovable rollback follows the existing fatal-divergence policy.

FocusIn and FocusOut use `Normal` mode. Root-to-child detail is `Inferior` then
`Ancestor`; child-to-root is `Ancestor` then `Inferior`; sibling changes use
`Nonlinear`. Events go only to clients selecting FocusChange on the exact
window; virtual ancestor events are not generated.

Ordinary map, unmap, destroy, cleanup, or policy replay may also change focus
while input mode is enabled. The server captures bounded old recipient facts
before commit so departure events remain deliverable even when a resource is
removed. Committed focus serials survive map, configure, replay, and focus
operations and are replayed across GWM and compositor restart.

