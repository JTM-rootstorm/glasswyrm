# Milestone 7 Drawable Model

Milestone 7 adds typed pixmap and graphics-context resources and canonical
pixel storage without creating an installed rendering ABI.

## Resource and drawable types

The server resource table distinguishes `Window`, `Pixmap`, and
`GraphicsContext`. All client-created IDs share one namespace. A collision with
any existing type, a server-owned ID, or an ID outside the client's assigned
base and mask is `BadIDChoice`. Request-specific lookups preserve exact errors
such as `BadPixmap`, `BadGContext`, and `BadDrawable`.

Supported raster drawables are depth-24 pixmaps and direct-root InputOutput
windows. Root, InputOnly, and child windows are not raster drawables in this
milestone. GCs retain screen and depth compatibility rather than borrowed
pointers to the drawable used at creation.

## Canonical format and storage

Every supported drawable uses XRGB8888 with depth 24, 32 bits per pixel,
32-bit scanline padding, and `width * 4` stride. Logical pixels are:

```text
31-24  deterministic X byte, always 0xff
23-16  red
15-8   green
7-0    blue
```

Canonical storage is private heap memory owned by `glasswyrmd`. Allocation
arithmetic is checked, initialization is deterministic opaque black, and no
uninitialized byte is exposed. Pixmaps always own canonical storage. Top-level
windows allocate it lazily on first drawing, first software-content map,
storage-preserving resize, or compositor replay.

The implementation limits each dimension to 16,384, each canonical drawable to
64 MiB, and published storage to 256 MiB. Allocation failures leave existing
state unchanged and report `BadAlloc` at the X11 boundary. Request framing and
the resource table provide additional bounds without implying an unbounded
allocation path.

## Window backgrounds

A window stores an explicit background source:

- `None` leaves pixels unchanged when clearing;
- `ParentRelative` resolves to opaque root black for a direct-root window; or
- `Pixel` fills with the stored lower 24 RGB bits.

`CWBackPixmap` selects `None` or `ParentRelative`; `CWBackPixel` selects
`Pixel`. The default is `None`. Milestone 7 does not implement general ancestor
alignment. Window borders are not rasterized; published dimensions describe
the interior drawable.

## Resize behavior

An applied top-level resize allocates and initializes a replacement store,
copies the upper-left overlap, and discards clipped pixels on shrink. Growth
adds deterministic right and bottom strips. The canonical store, matching
published buffer, damage, old-buffer retirement, and exposure rectangles are
staged as one lifecycle mutation. They become committed only after policy and
compositor acceptance; rejection preserves the old geometry and pixels.

## Ownership and cleanup

Pixmap and GC cleanup is synchronous on explicit free or owner disconnect.
Unpublished window storage dies with the window. Removing a published window
first commits scene removal; its retired memfd remains alive until the
compositor releases it or disconnects. A disconnected client's resource base
is not reused until coordinated top-level cleanup has committed and all XIDs
have been removed.
