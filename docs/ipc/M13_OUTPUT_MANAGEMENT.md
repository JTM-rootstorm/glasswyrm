# Milestone 13 output-management IPC

Milestone 13 extends installed GWIPC API 0 to version 0.8.0 while retaining
SOVERSION 0 and wire version 1.0. Existing API 0.1-0.7 symbols, message IDs,
and wire bytes remain unchanged. The additive public declarations are installed
as `<glasswyrm/ipc/output.h>` and included by the umbrella header.

## Capability profiles

The new capability bits are `OutputManagement`, `MultiOutputPolicy`,
`SurfaceOutputMembership`, `ScaleAwareSurfaces`, and `OutputControl`.
`ScaleMetadata` remains required by applicable output-model profiles.

- ProtocolServer-to-compositor output-model connections require output
  management, surface membership, and scale metadata.
- ProtocolServer-to-GWM connections require window policy, multi-output
  policy, and scale metadata.
- A separate control listener accepts same-UID `DiagnosticTool` peers with
  output control, mode `0600`, no file descriptors, and at most eight peers.

Capabilities are negotiated as complete profiles. Historical peers do not
receive the new records and continue using the singleton output and historical
policy hashes.

## Records

The additive wire registry is:

| ID | Record |
| ---: | --- |
| `0x0102` | `OutputDescriptorUpsert` |
| `0x0103` | `OutputModeUpsert` |
| `0x0113` | `SurfaceOutputState` |
| `0x0204` | `PolicyOutputUpsert` |
| `0x0205` | `PolicyWindowOutputHint` |
| `0x0500` | `OutputStateQuery` |
| `0x0501` | `OutputConfigurationCommit` |
| `0x0502` | `OutputConfigurationAcknowledged` |

An inventory query returns one Outputs-domain snapshot containing bounded
descriptors, modes, and current `OutputUpsert` states, followed by a correlated
acknowledgement. Optional query flags request descriptor, mode, layout, and
window information. All inventory records carry snapshot-item framing.

A configuration peer sends an Outputs-domain snapshot containing the complete
desired `OutputUpsert` set, then an acknowledgement-required commit with a
configuration ID, exact base generation, and primary output. The result is one
of Accepted, StaleGeneration, Busy, InvalidLayout, UnknownOutput,
UnsupportedMode, UnsupportedScale, UnsupportedTransform, PolicyRejected,
CompositorRejected, PresenterRejected, or InternalError. The correlated
acknowledgement reports applied generation, primary output, root logical size,
and enabled-output count.

Query and configuration IDs are checked independently of generic reply
sequences. A mismatch or malformed record closes only the offending control
peer. Every M13 output management and control message carries zero file
descriptors.

## Surface and policy state

`SurfaceOutputState` records one surface primary output, ordered complete
membership, preferred rational scale, integer client buffer scale, legacy or
scaled-pixmap mode, and layout generation. It is required exactly once for
each nonmetadata surface and cursor in an M13 complete scene.

`PolicyOutputUpsert` carries output logical and work rectangles, scale,
transform, enabled and primary state. `PolicyWindowOutputHint` carries previous
and preferred output IDs. These records participate in deterministic policy
hash v3; v1 and v2 remain byte-identical.

See [GWIPC API 0](GWIPC_API_0.md), [wire version 1](GWIPC_WIRE_V1.md), and
[layout transactions](../output/M13_LAYOUT_TRANSACTIONS.md).
