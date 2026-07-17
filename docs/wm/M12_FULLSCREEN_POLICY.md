# Milestone 12 fullscreen policy

Milestone 12 keeps X11/EWMH interpretation in `glasswyrmd` and deterministic
placement, focus, and stacking decisions in `gwm`. `gwm` does not become an
X11 client and does not perform rendering or direct scanout.

## Applied state

For managed direct-root windows, state precedence remains:

```text
minimized > fullscreen > maximized > normal
```

A fullscreen window occupies the complete published work area, is ineligible
for decorations, and is placed above non-fullscreen managed roots. A visible
transient remains above its parent and may take focus ahead of the fullscreen
root. Otherwise, a mapped fullscreen root is preferred for focus even when an
ordinary window has a newer map serial.

Leaving fullscreen uses the normal geometry supplied by `glasswyrmd`; GWM
clamps it through the same work-area policy as any other committed windowed
geometry. This keeps the saved/restore transition in protocol-server truth
while retaining a pure policy transform in GWM.

## Borderless windowed state

`decoration_preference=False` makes an ordinary managed window decoration
ineligible without changing its applied state, requested size, placement, or
stack band. It does not imply fullscreen. Glasswyrm still does not render
decorations in Milestone 12; the field is policy and compatibility metadata.

## Presentation eligibility

Visible managed fullscreen state is reported as `fullscreen_eligible=True`.
Override-redirect windows retain `Unknown` because GWM does not manage them.

Direct-scanout eligibility is computed separately. Non-fullscreen, hidden, or
unfocused managed windows report `False`. A focused fullscreen window reports
`Unknown`, because GWM cannot inspect compositor opacity, occlusion, buffer
format, or scanout constraints. This is deliberate: Milestone 12 performs no
direct scanout and does not claim eligibility that only `gwcomp` could prove.

## Determinism and tests

Fullscreen banding and focus ranks are part of the canonical policy order and
hash. Policy tests cover state precedence, work-area geometry, managed-stack
priority, transient focus, borderless windowed behavior, geometry restoration
inputs, and honest direct-scanout uncertainty.
