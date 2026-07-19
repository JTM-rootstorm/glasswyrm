# Milestone 14 GW_VRR protocol

`GW_VRR` version 0.1 is an experimental, opt-in application-preference and
state-observation protocol. It is present only when `glasswyrmd` is built with
experimental extensions and launched with `--output-model --vrr-protocol`.
It does not imply toolkit integration or direct application control of KMS.

## Registry

| Name | Major opcode | First event | Events | First error | Errors | Version |
|---|---:|---:|---:|---:|---:|---:|
| GW_VRR | 136 | 70 | 1 | 141 | 2 | 0.1 |

Errors are `BadPreference` at absolute code 141 and `BadWindow` at absolute
code 142. Request minor opcodes are:

| Minor | Request |
|---:|---|
| 0 | QueryVersion |
| 1 | SelectInput |
| 2 | GetWindowPreference |
| 3 | SetWindowPreference |
| 4 | GetWindowState |
| 5 | GetOutputState |

All requests have exact fixed lengths. Unused fields and padding are zero.
Multi-byte fields follow the requesting client's byte order; 64-bit values are
encoded as high and low CARD32 words.

## Preferences and ownership

Preferences are `Default=0`, `Disable=1`, `Allow=2`, and `Prefer=3`.
`SetWindowPreference` and `SelectInput` require a client-owned, direct-root
InputOutput window. Read requests may inspect another valid direct-root
InputOutput window. Preference state is removed with the window and is never
persisted.

A Set request is a lifecycle transaction. Its success reply and notifications
are deferred until GWM and compositor acceptance. Rejection returns a protocol
error and leaves the previous preference and state committed.

## Queries

`GetWindowPreference` reports the window, current preference, and current
RANDR primary-output XID. `GetWindowState` additionally reports policy
eligibility, candidate selection, effective output state, reason mask, policy
generation, and output-state generation.

`GetOutputState` accepts a current RANDR output XID and reports the requested
policy, connector-property presence, hardware capability, controllability,
simulation and effective flags, optional range, selected window, reason mask,
state generation, and latest interval. An unknown output uses `BadWindow` in
this bounded experimental profile.

## VrrNotify

The fixed 32-byte event uses event code 70. Clients select any combination of:

| Bit | Meaning |
|---:|---|
| 0 | application preference changed |
| 1 | policy eligibility or selected candidate changed |
| 2 | effective output state changed |

The detail byte carries selected change bits; bit 7 reports current effective
enabled state. The event also carries window, output, preference, policy,
reason mask, and output-state generation. Notifications are generated only
after transaction commit and only for subscribed state that changed.

Both little- and big-endian request, reply, error, and event paths are covered
by raw protocol tests. `GW_VRR` does not add PRESENT timing, pacing, DRI3,
DMA-BUF, or a persistent game profile.
