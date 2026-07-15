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
- Pointer, keyboard, automatic, and passive-button grabs as defined in
  [the grab document](../input/M11_GRABS.md).
- `QueryKeymap`, core keyboard mapping/modifier replies, bounded keyboard
  control, and bell behavior.

The implementation retains M9 core fonts/text, properties/atoms, child-window
composition, CopyArea scrolling, focus/exposure behavior, and absent optional
extension replies.

## Intended client tier

The sole external target is xterm patch 410 using the exact core-font ASCII
build and launch profile. Live normalized traces, interaction evidence, and
the DRM VM gate are not yet accepted, so this document records an implemented
protocol profile rather than a completed client claim.

Unsupported behavior includes the XKB extension, XIM/compose, Xft/Unicode,
arbitrary layouts, full synchronous/replay/confinement grabs, complete cursor
fonts, clipboard persistence, drag-and-drop, decorations, and multiple
workspaces or outputs.
