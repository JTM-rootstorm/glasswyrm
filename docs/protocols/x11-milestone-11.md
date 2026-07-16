# X11 Milestone 11 Profile

Milestone 11 extends the existing integrated software-content profile. It is
not a broad X11 compatibility declaration.

## Added core behavior

- Real-input core key, button, motion, crossing, focus, and wheel-as-button
  delivery using the M11 keyboard and grab models.
- Cursor opcodes 93-97 for the bounded pixmap/glyph/recolor/free/best-size
  subset and `CWCursor` inheritance.
- `SetSelectionOwner`, `GetSelectionOwner`, `ConvertSelection`, and a narrow
  `SendEvent` path for validated SelectionNotify and ClientMessage events.
- Exact PropertyNotify, SelectionClear, SelectionRequest, SelectionNotify, and
  ClientMessage encoding in each client's byte order.
- Pointer, keyboard, automatic, and `GrabButton` passive-button grabs as
  defined in [the grab document](../input/M11_GRABS.md).
- `QueryKeymap`, core keyboard mapping/modifier replies, bounded keyboard
  control, and bell behavior.
- The Xaw scrollbar's exact `CreateGC` subset: `FillOpaqueStippled` with a
  retained same-screen depth-1 `GCStipple`, and patterned
  `PolyFillRectangle` rendering anchored at the drawable origin.
- `CreateGC` accepts nested depth-24 `InputOutput` windows as drawables while
  preserving `BadMatch` for `InputOnly` and incompatible-depth windows.
- Setup advertises depth-1 pixmaps as one bit per pixel with 32-bit scanline
  padding and an allowed depth with no visuals. `PutImage` accepts the pinned
  xterm/Xaw depth-1 `ZPixmap` upload as LSBFirst, 32-bit-padded rows; source
  bits are copied directly through the depth-one GC plane mask. The existing
  depth-1 `XYBitmap`/`XYPixmap` and depth-24 `ZPixmap` paths are unchanged.

The implementation retains M9 core fonts/text, properties/atoms, child-window
composition, CopyArea scrolling, focus/exposure behavior, and absent optional
extension replies.

## Accepted client tier

The sole validated external target is xterm patch 410 using the exact
core-font ASCII build and launch profile. Its live normalized trace,
interaction evidence, VT/restart recovery, DRM frames, graphical-console
screenshots, restoration, and archive pass the fixed VM gate.

Unsupported behavior includes the XKB extension, XIM/compose, Xft/Unicode,
arbitrary layouts, `UngrabButton`, passive key grabs, full
synchronous/replay/confinement grabs, complete cursor fonts, clipboard
persistence, drag-and-drop, decorations, and multiple workspaces or outputs.
Tiled and transparent-stippled fills remain unsupported;
the opaque-stippled GC is accepted only by `PolyFillRectangle`, while line,
segment, polygon, and arc drawing reject that GC with `BadImplementation`.
