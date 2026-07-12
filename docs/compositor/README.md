# Headless Compositor Documentation

Milestone 4 establishes a deterministic, hardware-independent compositor path.
The documents here define its implemented contract:

- [Headless compositor](M4_HEADLESS_COMPOSITOR.md): topology, lifecycle,
  protocol subset, bounds, acknowledgements, and validation.
- [Pixel formats](PIXEL_FORMATS.md): exact XRGB8888 and premultiplied ARGB8888
  interpretation and integer blending.
- [Frame dumps](FRAME_DUMPS.md): binary PPM layout, manifest, hash, and golden
  update procedure.
- [M6 metadata-only surfaces](M6_METADATA_SURFACES.md): ProtocolServer scene
  metadata, policy pairing, manifest behavior, and the no-raster boundary.

Milestone 4 is complete. The repository proves the scene, read-only buffer
mapping, renderer, output, dump, producer lifecycle, release, rejection, and
reconnect paths through strict and sanitizer tests plus the Gentoo VM acceptance
workflow. X11 mapping, window-management policy, input, and hardware output
remain later milestones.
