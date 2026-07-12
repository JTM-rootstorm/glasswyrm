# X11 Milestone 7 Drawable and Raster Profile

Milestone 7 adds a deliberately narrow drawable and software-raster profile to
the existing local X11 service. Compatibility claims are limited to the
requests, resources, events, and tests described here.

## Supported drawables and resources

Raster operations support:

- depth-24 pixmaps; and
- direct-root InputOutput windows.

Windows, pixmaps, and graphics contexts share the client's XID namespace and
are checked by exact resource type. GCs may be used across clients while their
owner keeps them alive, but the GC and drawable must refer to the same screen
and depth. Pixmaps are canonical server resources and are never published
directly to the compositor.

## Supported requests

| Opcode | Request | Milestone 7 behavior |
|---:|---|---|
| 14 | `GetGeometry` | Retains window behavior and reports pixmap root, zero origin and border, dimensions, and depth. |
| 53 | `CreatePixmap` | Creates a nonempty depth-24 XRGB8888 pixmap using a valid same-screen drawable anchor. |
| 54 | `FreePixmap` | Frees an exact pixmap resource. |
| 55 | `CreateGC` | Creates a depth-24 GC after atomic validation of the supported value mask. |
| 56 | `ChangeGC` | Atomically applies supported GC fields. |
| 60 | `FreeGC` | Frees an exact graphics-context resource. |
| 61 | `ClearArea` | Applies the direct-root window background subset and optionally emits `Expose`. |
| 62 | `CopyArea` | Copies between supported pixmaps and windows, including overlap-safe same-storage copies. |
| 70 | `PolyFillRectangle` | Applies ordered, clipped solid fills using GC foreground. |
| 72 | `PutImage` | Accepts depth-24, 32-bpp, scanline-pad-32 ZPixmap data with zero left pad. |

Supported GC fields are function (`GXcopy` only), plane mask, foreground,
background, fill style (`FillSolid` only), subwindow mode (`ClipByChildren`
only), graphics exposures, clip origins, and clip mask (`None` only). Clip
origins are stored but inert without a clip mask. Supported request fields are
validated before state mutation; valid but deferred image formats or GC
features return `BadImplementation` where the profile defines that response.

## Image and raster rules

Request headers follow the requesting client's negotiated byte order. ZPixmap
payload pixels always follow the server image byte order advertised at setup,
which is LSBFirst. A big-endian client therefore still sends each 32-bit pixel
in little-endian image order. Incoming high bytes are ignored and stored pixels
force the X byte to `0xff`.

All supported drawing clips signed local coordinates to the drawable interior.
The effective plane mask is the GC mask's lower 24 bits. Empty intersections
are successful no-ops. `CopyArea` uses temporary storage when source and
destination share overlapping backing storage.

## Supported events

The server encodes these 32-byte core events in each recipient's byte order:

- `Expose` (12);
- `GraphicsExpose` (13); and
- `NoExpose` (14).

`Expose` is routed only to live clients selecting `ExposureMask` on the target
window, without propagation. It is generated after `MapNotify` when a
top-level InputOutput window becomes viewable, after `ConfigureNotify` for
newly exposed growth strips, and for a nonempty `ClearArea` request whose
exposures byte is one. Background source `None` still permits the requested
clear exposure even though pixels do not change.

When `CopyArea` uses a GC with graphics exposures enabled, a complete copy
produces one `NoExpose`. Missing requested destination coverage caused by
source bounds produces deterministic `GraphicsExpose` rectangles with the
remaining-event count. Obscuration does not create missing source pixels in
this canonical-backing profile.

## Explicitly unsupported

- root drawing;
- child-window drawing or composition;
- InputOnly drawing;
- depth-1 pixmaps or other drawable depths;
- `GetImage` and `CopyPlane`;
- bitmap or XYPixmap `PutImage`;
- text and font requests;
- tiles, stipples, dash patterns, and clip pixmaps;
- non-GXcopy raster functions;
- `CopyGC`, `SetDashes`, and `SetClipRectangles`;
- input, focus events, and normal application compatibility.

Borders remain stored window attributes but are not rendered. This profile is
proof of a deterministic toy-client raster path, not broad X11 compatibility.
