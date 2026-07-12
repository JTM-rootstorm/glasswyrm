# X11 Milestone 6 Profile

This is a narrow local, headless compatibility profile, not a claim that normal
X11 applications are supported.

## Implemented profile

- `ChangeWindowAttributes` for the documented attribute subset, including
  event selection and override-redirect;
- `GetWindowAttributes`;
- top-level `MapWindow`, `UnmapWindow`, and `ConfigureWindow` geometry;
- `ConfigureWindow` Above and Below policy metadata;
- StructureNotify and SubstructureNotify selections;
- `MapNotify`, `UnmapNotify`, `ConfigureNotify`, and `DestroyNotify` encoders
  and routing primitives;
- policy-backed geometry, tree ordering, and input-focus queries in integrated
  transactions;
- both X11 byte orders and exact per-recipient event sequences.

Lifecycle operations are deferred behind a per-client barrier until policy and
compositor metadata are accepted. Events are emitted only for committed state
transitions. A no-op map, unmap, or configure must not emit a structural event.

## Explicitly unsupported

- SubstructureRedirect as an external X11 window-manager path;
- `MapRequest`, `ConfigureRequest`, focus, expose, gravity, circulate, and
  reparent events;
- drawing, client pixmaps as mapped content, input, and reparenting;
- `MapSubwindows` and `UnmapSubwindows`;
- TopIf, BottomIf, and Opposite stack modes;
- broad toolkit or normal desktop application compatibility.

Mapped top-level windows have metadata and policy state but no client content.
Authorization remains the earlier unauthenticated local research boundary.

The raw three-process integration test exercises little- and big-endian
map/configure/unmap barriers, queries, event sequences, and no-op suppression.
Create/destroy and restart scenario coverage is still being completed; support
claims should follow passing acceptance evidence.

