# Milestone 11 Selections

The server maintains selection ownership keyed by atom. Each record contains
the owner client, owner window, and last-change timestamp; contents remain in
the owning client. Arbitrary atoms are accepted, including PRIMARY, CLIPBOARD,
TARGETS, UTF8_STRING, TEXT, STRING, COMPOUND_TEXT, and TIMESTAMP.

`SetSelectionOwner` validates the owner window and timestamp, ignores stale
changes, and sends `SelectionClear` to a replaced owner. Window destruction or
client cleanup clears matching ownership. `GetSelectionOwner` reports the
current window or `None`.

`ConvertSelection` sends `SelectionRequest` to the owner. With no owner it
sends `SelectionNotify` with property `None`. The owner writes the requestor's
property and returns a validated synthetic `SelectionNotify` through the
narrow `SendEvent` path. The server neither inspects nor transforms selection
payload bytes. Property changes generate `PropertyNotify` for interested
clients.

The accepted boundary is xterm PRIMARY selection and middle-button paste plus
the repository CLIPBOARD probe serving TARGETS and UTF8_STRING. The fixed VM
route validates those exchanges in the pinned xterm patch 410 core-font ASCII
profile. Clipboard-manager persistence, payload conversion, and drag-and-drop
are not implemented.
