# Software Reference Renderer

This directory owns Glasswyrm's deterministic scalar rendering path. The
pixel primitives implement XRGB copy and premultiplied-ARGB composition with
exact integer rounding. `SoftwareSceneRenderer` applies those primitives to a
validated compositor scene and returns the canonical component-neutral
`SoftwareFrame`.

The software scene renderer is the default renderer, has no EGL dependency,
and remains the reference for historical frame hashes and future accelerated
renderer equivalence. Presentation backends consume its result through the
separate output backend interface.
