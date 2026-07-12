# Headless Compositor Documentation

Milestone 4 establishes a deterministic, hardware-independent compositor path.
The documents here define its intended contract and record the portion that is
currently implemented:

- [Headless compositor](M4_HEADLESS_COMPOSITOR.md): topology, lifecycle,
  protocol subset, bounds, acknowledgements, and validation.
- [Pixel formats](PIXEL_FORMATS.md): exact XRGB8888 and premultiplied ARGB8888
  interpretation and integer blending.
- [Frame dumps](FRAME_DUMPS.md): binary PPM layout, manifest, hash, and golden
  update procedure.

These documents do not declare Milestone 4 complete. The reusable scene,
read-only buffer mapping, renderer, output, and dump primitives and the
production listener exist; reactor-side buffer attachment, the end-to-end
synthetic producer, and accepted-frame presentation remain deferred.
