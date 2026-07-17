# Milestone 12 RENDER and COMPOSITE Target Profile

This note freezes the bounded RENDER 0.11 and COMPOSITE 0.4 subsets specified
for Milestone 12. It is a target contract, not an implementation or test-proof
claim. Broad extension compatibility must not be inferred from the versions
advertised by this profile.

## RENDER 0.11

### Canonical formats and resources

Milestone 12 adds depth-8 and depth-32 pixmaps and defines four canonical
formats:

| Format | Canonical representation |
|---|---|
| A1 | depth 1 |
| A8 | depth 8 |
| XRGB32 | drawable depth 24, 32 bits per pixel |
| ARGB32 | depth 32, premultiplied alpha |

RENDER adds a client-owned `Picture` resource. A picture source is either
drawable-backed or a solid fill. Drawable-backed pictures refer to canonical
server drawable storage; the extension does not introduce an independent
pixel representation.

### Requests

The planned request surface is:

- `QueryVersion`;
- `QueryPictFormats`;
- `QueryPictIndexValues`;
- `CreatePicture`;
- `ChangePicture`;
- `SetPictureClipRectangles`;
- `FreePicture`;
- `Composite`;
- `FillRectangles`; and
- `CreateSolidFill`.

Only these picture attributes are supported:

- repeat mode `None`;
- no alpha map;
- clip origin;
- a clip region supplied by `SetPictureClipRectangles`;
- component alpha disabled; and
- subwindow mode `ClipByChildren`.

Transforms, filters, other repeat modes, alpha maps, and any unsupported
attribute are rejected without partially mutating the picture. Resource,
format, drawable, operator, and attribute validation must complete before an
operation changes pixels or damage state.

### Composite and fill semantics

`Composite` supports only the `Src` and `Over` operators. The source picture
may be drawable-backed or a solid fill, while the destination must be
drawable-backed. The mask picture must be `None`.

Coordinates are integral. There is no transform or repeat behavior. Source
and destination rectangles are clipped, ARGB values use premultiplied-alpha
semantics, and every accepted destination mutation produces canonical
drawable damage.

`FillRectangles` likewise supports only `Src` and `Over`, using one
premultiplied color and a bounded rectangle list. RENDER raster operations are
first defined by a deterministic scalar internal reference implementation.
The core raster path and extension tests must share its exact integer-rounding
rules.

### RENDER deferrals

The Milestone 12 profile does not implement:

- glyph sets or `CompositeGlyphs`;
- trapezoids or triangles;
- gradients;
- convolution filters;
- transforms;
- arbitrary masks; or
- cursor rendering through RENDER.

## COMPOSITE 0.4

### Requests and redirection

The planned request surface is:

- `QueryVersion`;
- `RedirectWindow`;
- `RedirectSubwindows`;
- `UnredirectWindow`;
- `UnredirectSubwindows`; and
- `NameWindowPixmap`.

Glasswyrm already renders X11 windows into server-owned backing storage before
final composition. COMPOSITE therefore records redirection state without
creating a second pixel pipeline. The bounded profile supports `Automatic`
and `Manual` redirection.

There is one manual redirect owner per window or subtree. A conflicting manual
redirect returns `BadAccess`. Automatic redirects may coexist within this
subset. Unredirect requests validate ownership before changing state, and all
redirection state owned by a client is removed when that client disconnects.
Failed ownership or mode validation must not partially change redirection.

### Named-window-pixmap lifetime

A successful `NameWindowPixmap` creates a client-named Pixmap resource that
references the window's current canonical storage object. Its lifetime is
defined as follows:

- unmapping the window does not invalidate the named pixmap;
- window drawing updates the named pixmap while the window retains the same
  storage object;
- resizing the window replaces its storage object;
- named pixmaps created before that replacement retain the old storage
  snapshot;
- freeing the pixmap releases its storage reference; and
- destroying the window does not invalidate an existing named pixmap.

This requires shared immutable-or-copy-on-replacement storage ownership. A
named pixmap must never retain a borrowed pointer into a window resource.

### COMPOSITE deferrals

The Milestone 12 profile does not implement:

- `GetOverlayWindow` or `ReleaseOverlayWindow`;
- overlay surfaces;
- compositor-manager selection; or
- client-controlled final desktop composition.

Final display authority remains with `gwcomp`; COMPOSITE clients cannot replace
or take ownership of the desktop composition pipeline.

## Proof status

No implementation or acceptance proof is claimed by this document. RENDER and
COMPOSITE should be reported as supported only after their bounded request
surfaces, error paths, resource lifetimes, exact scalar pixels, damage, cleanup,
both client byte orders, and malformed-request isolation have dedicated tests.
