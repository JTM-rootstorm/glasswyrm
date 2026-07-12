# Milestone 7 Reference Raster Profile

This profile defines deterministic core X11 raster behavior in `glasswyrmd`.
It is a correctness path, not a performance claim.

## Pixel operation

Only `GXcopy` and `FillSolid` are supported. For every destination pixel:

```text
mask = gc.plane_mask & 0x00ffffff
new_rgb = (source_rgb & mask) | (old_rgb & ~mask)
stored = 0xff000000 | new_rgb
```

Foreground, background, and image inputs use their lower 24 RGB bits. The X
byte is always forced to `0xff`. Unsupported raster functions are rejected
rather than approximated.

Every operation validates request framing, resources, depth, format,
arithmetic, and required scratch storage before mutation. Signed local
coordinates are clipped to drawable bounds. Empty intersections succeed
without damage.

## PutImage

The supported request is ZPixmap, depth 24, 32 bits per pixel, scanline pad 32,
and `left_pad` zero. Header fields follow client byte order, but payload pixels
follow the setup reply's server image byte order: LSBFirst. This remains true
for a big-endian client. Each row is exactly `width * 4` bytes, and the request
contains exactly the checked image payload with no truncation or trailing data.

Pixels are decoded explicitly as little-endian 32-bit values. The input high
byte is ignored, GXcopy and the plane mask are applied, and the clipped changed
destination is damaged. Bitmap and XYPixmap formats are deferred.

## PolyFillRectangle

Each eight-byte rectangle record is processed in request order. Zero-size
rectangles are skipped; negative origins and overhang are clipped. The GC
foreground and plane mask determine the result. Damage is the normalized union
of eligible clipped rectangles. More than 1,024 normalized rectangles collapse
to full drawable damage.

## CopyArea

Source and destination may independently be a supported pixmap or direct-root
InputOutput window. Source bounds are clipped first, translated coverage is
then clipped to the destination, and GXcopy with the destination GC plane mask
is applied. If both drawables share storage and overlap, the request uses a
temporary image so horizontal, vertical, and two-dimensional overlap have no
order-dependent behavior.

With graphics exposures disabled, no copy exposure event is sent. With them
enabled, complete requested destination coverage produces `NoExpose`.
Uncovered destination rectangles caused by source bounds produce sorted
`GraphicsExpose` events, ordered by y, x, height, and width; each count is the
number remaining. The major opcode is `CopyArea` and the minor opcode is zero.

## ClearArea

`ClearArea` targets a supported InputOutput window. Width or height zero extends
from the signed origin to the corresponding drawable edge before clipping.
`Pixel` backgrounds replace the clipped region with that RGB value;
direct-root `ParentRelative` uses opaque black; `None` leaves pixels unchanged.
Clear replacement does not use a GC plane mask.

An exposures byte of one emits one `Expose` for a nonempty clipped rectangle to
each live selector of `ExposureMask`, including for background `None`. Zero
emits none; other values are invalid. Map exposure covers the full interior
after `MapNotify`. Resize growth exposure uses deterministic nonoverlapping
right and bottom strips after `ConfigureNotify`.

All event fields use the recipient's negotiated byte order and the low 16 bits
of that recipient's last processed request sequence. Expose does not propagate.
