# Milestone 14 variable-refresh IPC

Milestone 14 extends installed GWIPC API 0 to version 0.9.0 while retaining
SOVERSION 0 and wire version 1.0. The extension is additive: API 0.1 through
0.8 symbols and all historical record bytes remain unchanged. The public VRR
types are installed as `<glasswyrm/ipc/vrr.h>` and included by the umbrella
header.

## Capabilities and records

VRR metadata, VRR policy, and presentation timing are negotiated independently
with capability bits 22 through 24. Output control remains the authority for a
diagnostic tool to query or propose output policy. A peer that does not
negotiate the new capabilities receives no M14 records.

| ID | Record | Payload bytes |
| ---: | --- | ---: |
| `0x0104` | `OutputVrrCapabilityUpsert` | 40 |
| `0x0105` | `OutputVrrPolicyUpsert` | 16 |
| `0x0106` | `OutputVrrStateUpsert` | 96 |
| `0x0114` | `SurfaceVrrState` | 56 |
| `0x0142` | `PresentationTiming` | 56 |
| `0x0206` | `PolicyWindowVrrUpsert` | 16 |
| `0x0207` | `PolicyOutputVrrUpsert` | 16 |
| `0x0214` | `PolicyWindowVrrState` | 40 |
| `0x0215` | `PolicyOutputVrrState` | 32 |

Every record has a fixed little-endian payload, zero file descriptors, exact
boolean and enum validation, zero reserved fields, and a reason mask containing
only named bits. Truncation, trailing bytes, unknown enum values, noncanonical
booleans, and unknown reason bits are protocol errors.

## Authority and snapshots

The compositor publishes output capability, effective state, and presentation
timing. The protocol server owns requested per-output policy and per-window
application preference. GWM receives complete policy inputs and returns its
window and output decisions. The compositor remains final display authority
and may reject a GWM-selected candidate for surface, session, or presenter
reasons.

Complete M14 snapshots require one output policy per output. Scene snapshots
carry one `SurfaceVrrState` for each nonmetadata window surface. Window-policy
snapshots carry the complete VRR input and result sets. Output-control snapshots
remain valid without VRR records when the corresponding M14 capabilities and
query flag were not negotiated.

| Record | Direction | Envelope rule |
| --- | --- | --- |
| output capability | Compositor to ProtocolServer; ProtocolServer to DiagnosticTool | `SnapshotItem` in Outputs |
| output policy | ProtocolServer to Compositor | `SnapshotItem` in CompleteSession |
| output policy | DiagnosticTool to ProtocolServer; ProtocolServer to DiagnosticTool | `SnapshotItem` in Outputs |
| output effective state | Compositor to ProtocolServer | `SnapshotItem` in Outputs, or intermediate `Reply` for a frame |
| output effective state | ProtocolServer to DiagnosticTool | `SnapshotItem` in Outputs |
| surface state | ProtocolServer to Compositor or DiagnosticTool | `SnapshotItem` in CompleteSession or Outputs respectively |
| GWM window/output input | ProtocolServer to WindowManager | `SnapshotItem` in WindowPolicy |
| GWM window/output result | WindowManager to ProtocolServer | `SnapshotItem` in WindowPolicy |
| presentation timing | Compositor to ProtocolServer | no flags outside a snapshot |
| presentation timing | ProtocolServer to DiagnosticTool | `SnapshotItem` in Outputs |

For a presented M14 frame, the compositor emits a bounded response batch in
this order:

1. `OutputVrrStateUpsert`;
2. `PresentationTiming`;
3. `FrameAcknowledged`;
4. any `BufferRelease` records.

The state record describes a committed and read-back property value, or an
explicitly simulated headless value. It is not positive hardware evidence.
Positive hardware evidence requires monotonic kernel page-flip timestamps that
demonstrate in-range cadence while VRR is effective and a changed cadence when
it is disabled.

## Compatibility proof

The staged install matrix compiles, links, and runs C and C++ consumers for API
0.1 through 0.9. The 0.9 consumers exercise the installed header, capability
bits, public codecs, decoded accessors, symbol versions, and API version. Wire
goldens cover all nine records plus malformed and semantic validation.

See [GWIPC API 0](GWIPC_API_0.md), [wire version 1](GWIPC_WIRE_V1.md), and the
[Milestone 13 output-management contract](M13_OUTPUT_MANAGEMENT.md).
