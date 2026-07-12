# Rendering Documentation

Milestone 7 establishes Glasswyrm's first server-rendered window-content path.
These documents define the implemented reference boundary:

- [Drawable model](M7_DRAWABLE_MODEL.md): typed resources, canonical storage,
  backgrounds, resize, limits, and cleanup.
- [Raster profile](M7_RASTER_PROFILE.md): exact pixel operations, clipping,
  image byte order, plane masks, and exposure behavior.
- [Content publication](M7_CONTENT_PUBLICATION.md): persistent published
  buffers, damage scheduling, lifecycle arbitration, release, and replay.

The compositor-facing profile is documented separately in
[M7 buffered ProtocolServer surfaces](../compositor/M7_BUFFERED_PROTOCOL_SURFACES.md).
`glasswyrmd` owns core X11 raster semantics and canonical pixels; `gwcomp`
continues to own final scene composition and frame output.
