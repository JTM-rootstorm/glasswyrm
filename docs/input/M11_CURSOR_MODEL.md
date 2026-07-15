# Milestone 11 Cursor Model

`glasswyrmd` owns cursor resources. A cursor image contains a bounded
premultiplied ARGB image, dimensions, hotspot, colors, and canonical kind.
Extent is limited to 64x64, each client may own at most 256 cursors, and total
cursor storage is limited to 4 MiB.

The core subset implements `CreateCursor` from depth-1 source/mask pixmaps,
`CreateGlyphCursor`, `FreeCursor`, `RecolorCursor`, cursor-class
`QueryBestSize`, and `CWCursor`. The built-in glyph subset covers the root
pointer, xterm text pointer, move, bottom-right resize, watch, the xterm
horizontal- and vertical-scrollbar pointers, and the audited fixed-font
hidden-cursor path. It is not a complete X cursor-font implementation.

`None` in `CWCursor` means inheritance. Effective selection walks from the
deepest viewable pointer target through its ancestors and falls back to the
root left pointer. A grab cursor and GWM interactive override take precedence.
Images use shared lifetime so freeing an XID invalidates the resource
immediately without invalidating an image still referenced by a window, grab,
or in-flight publication.

One server-owned cursor surface is published to `gwcomp` with ARGB8888
premultiplied content. Image changes create an immutable sealed memfd buffer;
movement reuses the buffer and changes position to pointer minus hotspot.
`gwcomp` accepts at most one cursor per output, renders it after all ordinary
surfaces, omits it from window/policy counts, and records deterministic cursor
metadata in the scene manifest. It remains part of the canonical software
frame and DRM scanout. Themed cursors and hardware cursor planes are deferred.
