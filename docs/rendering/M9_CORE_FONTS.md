# Milestone 9 core font profile

Milestone 9 provides one server-owned, deterministic core font for the bounded
`xclock` compatibility profile. It is not a general X font service.

## Identity and lifetime

The built-in font has 6-pixel advance, ascent 10, descent 3, and printable
ASCII coverage from byte 32 through 126. The accepted names are `fixed`,
`6x13`, and the bounded `-misc-fixed-*` form, matched case-insensitively.

Every graphics context starts with the server-owned default font. Selecting an
opened font snapshots its canonical built-in identity into the graphics
context, so closing the client's Font XID does not invalidate an existing GC.

## Supported core requests

- `OpenFont`, `CloseFont`, `QueryFont`, `QueryTextExtents`, and `ListFonts`;
- `GCFont` selection through `CreateGC` and `ChangeGC`;
- `ImageText8`, including the full-cell background fill;
- `PolyText8`, including signed deltas and Font shift items.

`QueryFont` returns one fixed metric record for each printable ASCII byte and
no font properties. `QueryTextExtents` accepts the core CHAR2B representation
only when byte1 is zero. Nonzero byte1 values are outside this milestone and
return `BadImplementation`.

Glyphs use a repository-owned 5-by-7 mark centered in each 6-by-13 cell.
Undefined bytes render a deterministic replacement box. Text drawing applies
the graphics context plane mask, retains an opaque X byte, clips to the
drawable, and reports the clipped cell bounds as damage.

## Intentional limits

The profile does not provide arbitrary XLFD wildcard matching, font paths,
font properties, scalable fonts, 16-bit glyph rendering, or
`ListFontsWithInfo`. Applications needing broad modern text support remain
outside the Milestone 9 compatibility tier.
