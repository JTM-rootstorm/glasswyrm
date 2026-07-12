# Milestone 8 Synthetic-Input Contract

GWIPC API 0.5.0 adds an opt-in DiagnosticTool contract while retaining
SOVERSION 0 and wire version 1.0.

| Message | ID | Flags | Descriptors |
|---|---:|---|---:|
| SyntheticMotion | `0x0300` | AckRequired | 0 |
| SyntheticButton | `0x0301` | AckRequired | 0 |
| SyntheticKey | `0x0302` | AckRequired | 0 |
| SyntheticBarrier | `0x0303` | AckRequired | 0 |
| SyntheticInputAcknowledged | `0x0310` | Reply | 0 |

All require `GWIPC_CAP_SYNTHETIC_INPUT`. Public C structures and typed
encode/decode functions are declared by `<glasswyrm/ipc/input.h>`, which is
also included by `<glasswyrm/ipc.h>`. Reserved fields and input flags must be
zero. Existing message IDs and payloads remain byte-identical.

The connection correlates each outgoing AckRequired sequence with its input
ID. A reply must match both `reply_to` and input ID; a mismatch closes only that
peer and clears both generic and specialized pending-reply state.

Motion coordinates are signed and may be outside the screen; the server clamps
them. Buttons are 1 through 5, keycodes are 8 through 255, transition values
are exactly zero or one, input IDs are nonzero, and event-record times are
nonzero. The result enum distinguishes Accepted, Clamped, InvalidTransition,
FocusUnchanged, FocusRejected, and LimitExceeded.

API 0.1 through 0.4 source consumers remain supported. Compositor, policy,
lifecycle, and rendering wire goldens remain unchanged.

