# Milestone 8 Synthetic Input

## Enabling the listener

Synthetic input is opt-in and integrated-mode only:

```text
glasswyrmd --display 99 --wm-socket /run/gwm.sock \
  --compositor-socket /run/gwcomp.sock --software-content \
  --synthetic-input-socket /run/glasswyrm-input/input.sock
```

There is no default input socket. The foreground server owns and removes the
filesystem endpoint. A standalone server, or a build without `libgwipc`,
rejects the option.

The listener accepts one same-UID GWIPC `DiagnosticTool` peer with
`GWIPC_CAP_SYNTHETIC_INPUT`. It uses the public structures in
`<glasswyrm/ipc/input.h>`: `gwipc_synthetic_motion`,
`gwipc_synthetic_button`, `gwipc_synthetic_key`,
`gwipc_synthetic_barrier`, and `gwipc_synthetic_input_acknowledged`.
GWIPC API is 0.5.0; SOVERSION remains 0 and wire remains 1.0.

## IDs, time, and state

Input IDs begin at one per connection and increase by exactly one. Zero,
duplicate, skipped, or decreasing IDs close only that provider. Motion,
button, and key time is nonzero and monotonic; equal times are permitted and
backwards time is a provider protocol error. State begins at pointer `(0,0)`,
root target and focus, no pressed keys or buttons, and logical time 1.
Coordinates are clamped to the root interior.

The queue accepts at most 4096 records and 1 MiB, and processes at most 64
records or 64 KiB per reactor turn. A new record beyond the queue limit is
acknowledged `LimitExceeded`; ordinary X11 work and runtime recovery remain
live. At most one input-produced focus operation awaits policy and later input
records remain ordered behind it.

## Acknowledgements and barriers

Every record is `AckRequired`. Its reply contains the input ID, current logical
time, resulting pointer position and target, committed focus, state mask, and
the count of X11 packets successfully enqueued. Immediate records acknowledge
after state changes and event enqueueing. A focus-producing click acknowledges
after policy and compositor commit or proven rollback. This does not promise
that an X11 socket has flushed.

A barrier emits no X11 event, does not advance time, and acknowledges only
after all earlier records and any earlier focus transaction complete.

Invalid button or key transitions return `InvalidTransition`, emit no event,
and do not advance time. On provider disconnect, queued records and pressed
key/button/modifier state are cleared. Pointer position, pointer target,
logical time, and committed GWM focus survive, and a new provider may connect.
An already-started focus transaction finishes without sending an absent peer
an acknowledgement.

## Test tool

The noninstalled public-API consumer has a fixed interface:

```text
gwinput_m8 --socket PATH --scenario NAME --output PATH
```

Scenarios are `barrier`, `motion`, `crossing`, `buttons`, `button-motion`,
`modifiers`, `keyboard`, `click-focus`, `invalid-transition`, `malformed`,
`queue-limit`, `reconnect`, and `m9-xeyes`. The last scenario is the fixed M9
xeyes acceptance path: left of the window, inside each eye, then a final root
position, with a barrier and bounded poll interval after every motion. Output
is deterministic JSON; arbitrary input scripts are intentionally unsupported.
