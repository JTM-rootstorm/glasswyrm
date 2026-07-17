# Milestone 8 Input

Milestone 8 adds an opt-in, headless synthetic-input path owned by
`glasswyrmd`.

- [Synthetic input](M8_SYNTHETIC_INPUT.md) documents the provider contract,
  state, ordering, and test tool.
- [Event routing](M8_EVENT_ROUTING.md) documents hit testing, selection,
  propagation, crossing, and coordinates.
- [Focus policy](M8_FOCUS_POLICY.md) documents click focus and its lifecycle
  transaction.
- [Modifier map](M8_MODIFIER_MAP.md) records the fixed raw-keycode map.

This is a deterministic test backend, not a real device stack.

Milestone 11 adds a separate opt-in real-device profile:

- [libinput backend](M11_LIBINPUT_BACKEND.md)
- [keyboard model and repeat](M11_KEYBOARD_MODEL.md)
- [grab subset](M11_GRABS.md)
- [cursor model](M11_CURSOR_MODEL.md)
