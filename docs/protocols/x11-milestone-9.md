# X11 Milestone 9 simple-client profile

Status: implemented protocol foundation; pinned-client acceptance pending.

This profile is intentionally bounded to `xeyes` 1.3.1 and `xclock` 1.2.0
under the exact commands documented in the compatibility notes. It is not a
claim of broad X11, Xt, Xaw, xeyes, or xclock compatibility.

## Implemented requests and subsets

- `QueryExtension` reports every queried extension absent; `ListExtensions`
  returns an empty list. Shape and Render have no opcodes.
- `AllocColor`, `AllocNamedColor`, `FreeColors`, `QueryColors`, and
  `LookupColor` operate only on the setup default TrueColor colormap. Values
  are quantized to eight bits per component. Named colors use the bounded
  built-in parser.
- `OpenFont`, `CloseFont`, `QueryFont`, `QueryTextExtents`, and `ListFonts`
  expose only the built-in fixed font profile. `ImageText8` and `PolyText8`
  render its bounded byte glyphs. See [the font profile](../rendering/M9_CORE_FONTS.md).
- `QueryPointer` reports current synthetic root coordinates, coordinates
  relative to the queried window, the immediate viewable child, and the core
  state mask. `TranslateCoordinates` supports window-to-window translation
  and immediate-child lookup. Unrepresentable signed-16 coordinates return
  `BadImplementation`.
- `GetKeyboardMapping` returns two keysyms per requested keycode for the fixed
  M9 map. `GetModifierMapping` returns two keycodes per modifier and
  `GetPointerMapping` returns buttons 1 through 5. These are query-only core
  maps, not XKB or remapping support.
- `CreatePixmap` accepts depth 1 and depth 24. Depth-1 storage is packed and
  bounded; `PutImage` accepts the implemented XYBitmap upload profile.
- `CreateGC` and `ChangeGC` add the bounded foreground, background, plane
  mask, line width, subwindow mode, and built-in-font selections required by
  the M9 request profile. Unsupported values remain explicit errors.
- `PolyLine` accepts origin and previous coordinate modes and draws one-pixel
  Bresenham segments. `PolySegment` draws independent one-pixel segments.
- `FillPoly` accepts only convex polygons in origin mode.
- `PolyFillArc` accepts only complete positive or negative 360-degree
  ellipses; partial arcs are unsupported.
- `ImageText8` paints full character cells before glyph marks. `PolyText8`
  accepts signed deltas and the implemented font-shift items.

All request decoding is byte-order aware. Resource and length errors remain
recoverable. Allocation failures become `BadAlloc`; unknown requests remain
`BadRequest`.

## Child-window presentation

Mapped InputOutput child windows have server-owned backing. Before publication,
the server flattens each viewable descendant into its direct-root top-level
buffer in child stacking order, clipped by every ancestor. Only that top-level
buffer crosses GWIPC. See [child composition](../rendering/M9_CHILD_WINDOW_COMPOSITION.md).

## Trace option

`glasswyrmd --x11-trace PATH` safely creates a new JSONL trace before opening
the display socket. Existing paths and symlinks are rejected. Records omit
authorization, property, image, and text payloads and stop at 64 MiB. The M9
normalizer classifies ordered requests, histograms, replies, recurring traffic,
extension queries, unknown opcodes, connection count, and maximum request size.

## Explicitly unsupported

- Shape, Render, Xft, XKB, locale font sets, and arbitrary server fonts;
- partial arcs, wide/dashed lines, general polygon shapes, and general
  graphics-context semantics;
- real input devices, grabs, cursor rendering, and remapping requests;
- broad Xt/Xaw or arbitrary-client compatibility;
- a claim that the pinned applications pass before frozen VM traces and frames
  are present.
