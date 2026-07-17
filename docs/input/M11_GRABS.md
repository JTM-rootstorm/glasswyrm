# Milestone 11 Grab Subset

Milestone 11 implements a deliberately bounded core grab model for xterm
selection and GWM interactions.

- A delivered button press may establish an automatic grab. Motion and release
  route to that client/window until all buttons are released; crossing remains
  based on the real pointer target.
- `GrabPointer`, `UngrabPointer`, and `ChangeActivePointerGrab` support one
  active pointer grab, owner-events selection, asynchronous modes,
  `confine_to=None`, a validated optional cursor, event-mask validation, and
  core timestamp/status behavior.
- `GrabKeyboard` and `UngrabKeyboard` support one asynchronous keyboard grab.
- `GrabButton` provides the passive-button subset observed in the pinned xterm
  profile.
- `AllowEvents` accepts only the documented asynchronous release modes.

Matching GWM move/resize bindings use an internal pointer grab and take
precedence over ordinary client button delivery. Grabs are aborted on relevant
client/window cleanup, interaction abort, peer loss, VT suspension, or input
loss.

`UngrabButton`, synchronous freezing, replay modes, confinement windows,
passive-key grabs, and complete X11 grab ordering are unsupported.
Applications must not infer a general X11 grab compatibility claim from this
subset.
