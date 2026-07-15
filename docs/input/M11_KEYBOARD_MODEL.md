# Milestone 11 Keyboard Model

The real-input keyboard engine uses libxkbcommon internally, but exposes only
core X11 keyboard behavior. Defaults are:

```text
rules=evdev model=pc105 layout=us variant="" options=""
repeat-delay=500ms repeat-rate=25Hz
```

The matching `glasswyrmd` options are `--xkb-rules`, `--xkb-model`,
`--xkb-layout`, `--xkb-variant`, `--xkb-options`, `--repeat-delay-ms`, and
`--repeat-rate-hz`. M11 validates configuration at startup; it does not offer
runtime layout switching.

Linux evdev keycodes become X11 keycodes by adding eight. For a transition,
the server captures the keysym and core modifier mask before applying the xkb
state update, matching core event-state semantics. The serialized core mask
uses Shift, Lock, Control, and Mod1-Mod5. `GetKeyboardMapping`,
`GetModifierMapping`, and `QueryKeymap` derive from the same engine rather than
the fixed M8 table.

Repeat uses a nonblocking monotonic timerfd. A repeatable press arms the timer;
dispatch produces a bounded release/press pair for the focused key and drops
excess expirations beyond the per-dispatch limit. Release, focus invalidation,
session suspension, device loss, and client cleanup cancel repeat.

The implemented core control surface includes the bounded
`ChangeKeyboardControl`, `GetKeyboardControl`, and `Bell` behavior documented
by the M11 protocol profile. No XKB server extension, XIM, compose/dead-key
processing, IME, or broad layout compatibility is claimed. The M8 synthetic
mapping remains unchanged and does not use this engine.
