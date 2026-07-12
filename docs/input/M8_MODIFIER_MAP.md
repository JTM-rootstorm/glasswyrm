# Milestone 8 Synthetic Modifier Map

The synthetic backend maps exactly these raw keycodes:

| Keycode | Label | State bit |
|---:|---|---|
| 37 | Control_L | ControlMask |
| 50 | Shift_L | ShiftMask |
| 62 | Shift_R | ShiftMask |
| 64 | Alt_L | Mod1Mask |
| 105 | Control_R | ControlMask |
| 108 | Alt_R | Mod1Mask |

A modifier stays set while any mapped key for it remains pressed. Lock and
Mod2 through Mod5 stay clear, and all other keycodes leave modifier state
unchanged.

This table is only the M8 deterministic test map. The server does not translate
keycodes to keysyms or text and does not implement keyboard mapping requests,
repeat, lock behavior, or XKB.

