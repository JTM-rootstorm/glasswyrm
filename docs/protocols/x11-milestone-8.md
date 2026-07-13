# X11 Milestone 8 Synthetic Input and Event Profile

Milestone 8 adds a deterministic opt-in input route for repository-owned
headless tests. It is not a claim of general application or real-device input
compatibility.

## Supported

- KeyPress and KeyRelease
- ButtonPress and ButtonRelease
- MotionNotify
- EnterNotify and LeaveNotify
- FocusIn and FocusOut
- top-level/root routing
- the documented propagation and do-not-propagate subset
- pointer, button, and fixed modifier state
- GWM-mediated Button 1 click focus

Packets use the recipient client's byte order, its last processed request
sequence, exact core 32-byte layouts, and zero padding. Hit testing includes
only direct-root, viewable, policy-visible InputOutput windows. See the
[routing contract](../input/M8_EVENT_ROUTING.md) for selection, coordinates,
state, crossing, and ordering details.

## Unsupported

- grabs, including automatic button grabs
- QueryPointer
- GetMotionEvents
- QueryKeymap
- keyboard mapping requests
- SetInputFocus
- XKB
- cursors
- child or InputOnly hit testing
- real devices

Raw keycodes have only the fixed synthetic modifier interpretation documented
for M8. There are no keysyms, text translation, repeat, lock semantics, or
cursor sprite.

