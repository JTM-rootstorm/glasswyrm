# Rendering Documentation

Milestone 7 establishes Glasswyrm's first server-rendered window-content path.
These documents define the implemented reference boundary:

- [Drawable model](M7_DRAWABLE_MODEL.md): typed resources, canonical storage,
  backgrounds, resize, limits, and cleanup.
- [Raster profile](M7_RASTER_PROFILE.md): exact pixel operations, clipping,
  image byte order, plane masks, and exposure behavior.
- [Content publication](M7_CONTENT_PUBLICATION.md): persistent published
  buffers, damage scheduling, lifecycle arbitration, release, and replay.
- [Milestone 9 core fonts](M9_CORE_FONTS.md): the bounded fixed-font identity,
  metrics, requests, glyph rasterization, and compatibility limits.
- [Milestone 9 child composition](M9_CHILD_WINDOW_COMPOSITION.md): recursive
  server-side child flattening without a GWIPC ABI change.
- [Milestone 12 renderer abstraction](M12_RENDERER_ABSTRACTION.md): renderer
  selection, the exact software reference path, component-neutral frames, and
  deterministic renderer reporting below the scene transaction.
- [Milestone 12 buffer synchronization](M12_BUFFER_SYNCHRONIZATION.md): GWIPC
  0.7 eventfd readiness ordering, ownership, failure, and replay semantics.
- [Milestone 12 EGL/GLES renderer](M12_EGL_GLES.md): the opt-in GLES 2.0
  profile, GBM and surfaceless context order, bounded texture cache,
  damage-aware upload/readback, fallback rules, and equivalence boundary.
- [Milestone 13 scale and transform](M13_SCALE_AND_TRANSFORM.md): rational
  output scale, integer client scale, sampling, damage, and all transforms.
- [Milestone 13 multi-output rendering](M13_MULTI_OUTPUT_RENDERING.md): atomic
  native frame sets, software/GLES paths, and aggregate identity.

The compositor-facing profile is documented separately in
[M7 buffered ProtocolServer surfaces](../compositor/M7_BUFFERED_PROTOCOL_SURFACES.md).
`glasswyrmd` owns core X11 raster semantics and canonical pixels; `gwcomp`
continues to own final scene composition and frame output.
