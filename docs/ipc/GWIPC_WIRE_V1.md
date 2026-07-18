# GWIPC Wire Version 1

Wire 1.0 uses one nonblocking Unix `SOCK_SEQPACKET` record per message. All
integers are fixed-width little-endian values. Native structures, padding, and
X11 client byte order never affect this protocol.

## Envelope

Every record starts with this 40-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `GWIP` |
| 4 | 2 | header size, 40 |
| 6 | 2 | wire major |
| 8 | 2 | wire minor |
| 10 | 2 | message type |
| 12 | 4 | flags |
| 16 | 4 | payload size |
| 20 | 2 | attached FD count |
| 22 | 2 | reserved, zero |
| 24 | 8 | sender sequence |
| 32 | 8 | reply-to sequence, or zero |

Payload size must equal the remaining record size and the declared descriptor
count must equal the received `SCM_RIGHTS` count. Established traffic uses the
negotiated version. Each direction starts at sequence 1 and advances exactly by
one. Sequence zero, gaps, duplicates, exhaustion, unknown flags, and nonzero
reserved fields are protocol errors.

Flags are `Reply` (bit 0), `Error` (bit 1), `AckRequired` (bit 2),
`SnapshotItem` (bit 3), and `Critical` (bit 4). A reply has a nonzero
`reply_to`; an error is also a reply. Snapshot items require an active incoming
snapshot.

## Roles And Capabilities

Roles are `Unknown=0`, `ProtocolServer=1`, `WindowManager=2`, `Compositor=3`,
`TestProducer=4`, `TestConsumer=5`, and `DiagnosticTool=6`.

The 64-bit capability bits are:

| Bit | Capability |
|---:|---|
| 0 | FdPassing |
| 1 | Snapshots |
| 2 | OutputState |
| 3 | SurfaceState |
| 4 | MemfdBuffers |
| 5 | DamageRegions |
| 6 | ScaleMetadata |
| 7 | SdrColorMetadata |
| 8 | FrameAcknowledgement |
| 9 | TraceMetadata |
| 10 | WindowPolicy |
| 11 | WindowLifecycle |
| 12 | SyntheticInput |
| 13 | SessionState |
| 14 | InteractivePolicy |
| 15 | CursorSurface |
| 16 | CpuBufferSynchronization |
| 17 | OutputManagement |
| 18 | MultiOutputPolicy |
| 19 | SurfaceOutputMembership |
| 20 | ScaleAwareSurfaces |
| 21 | OutputControl |

Unknown offered bits are ignored. Unknown required bits reject the handshake.
Negotiated capabilities are the offered intersection and must contain the
requirements of both peers. Buffer attachment requires FdPassing and
MemfdBuffers; snapshots, damage, and frame acknowledgements require their
corresponding capabilities.
Every policy message requires WindowPolicy and carries no descriptors.
OutputManagement protects compositor inventory and server/compositor output
queries. MultiOutputPolicy is combined with WindowPolicy and ScaleMetadata.
SurfaceOutputMembership is combined with ScaleMetadata. ScaleAwareSurfaces
activates client-buffer-scale semantics while ScaleMetadata continues carrying
the rational values. OutputControl is restricted to the same-UID
DiagnosticTool listener. Every message added in API 0.8 carries zero
descriptors.

Wire 1.0's registry is intentionally additive: a new message ID is usable only
when protected by a negotiated capability. Peers that do not offer
WindowPolicy continue using the unchanged M3/M4 registry; peers requiring an
unknown or unavailable capability fail negotiation rather than interpreting a
new payload.

## Limits

Defaults are a 64 KiB payload, 4 FDs per message, 1 MiB and 256 messages queued
per peer, a 256-byte diagnostic, a 64-byte instance label, 1,024 damage
rectangles, and one active snapshot per direction. Hard ceilings are a 1 MiB
payload, 16 FDs, and 16 MiB queued output. Peers negotiate the minimum offered
payload and FD limits. All size and geometry arithmetic is checked.

The output registry additionally limits a layout to 8 outputs, an output name
to 63 bytes, an output to 128 modes, each physical dimension to 4,096 pixels,
each output to 16,777,216 pixels, all enabled outputs to 67,108,864 pixels, and
the logical root to 32,767 by 32,767. Output scales are reduced rationals from
1/1 through 4/1 with denominator at most 120. The server accepts at most eight
active same-UID output-control peers.

## Handshake And Control

No application record is delivered before establishment.

`Hello` (`0x0001`) is sequence 1 and contains:

```text
u16 minimum major, minimum minor, maximum major, maximum minor
u16 sender role; u16 reserved
u64 offered capabilities; u64 required capabilities
u32 maximum payload; u16 maximum FD count; u16 name length
u8 instance_id[16]; u8 name[name length]
```

The version range is ordered, the instance ID is nonzero, the bounded label is
valid UTF-8, and no descriptors are attached.

`Welcome` (`0x0002`) is a reply to Hello:

```text
u16 selected major, selected minor, sender role, reserved
u64 negotiated capabilities
u32 negotiated maximum payload; u16 negotiated maximum FDs; u16 reserved
u64 connection ID; u8 instance_id[16]
```

The connection ID and instance ID are nonzero. Wire 1.0 requires overlapping
major ranges and selects the highest common implemented minor.

`Reject` (`0x0003`) is a reply containing:

```text
u16 reason; u16 detail length
u16 supported minimum major, minimum minor, maximum major, maximum minor
u32 reserved; u8 detail[detail length]
```

Reasons are IncompatibleVersion, RoleNotAllowed, CapabilityMismatch,
CredentialRejected, InvalidHello, ServerBusy, and InternalError. The listener
flushes a safe rejection when possible, then closes.

`Ping` (`0x0004`) and `Pong` (`0x0005`) contain one `u64 nonce`; Pong replies to
the matching Ping.

`ProtocolError` (`0x0006`) contains:

```text
u16 error; u16 offending type; u32 reserved
u64 offending sequence; u16 detail length; u16 reserved
u8 detail[detail length]
```

Errors are MalformedEnvelope, MalformedPayload, UnsupportedMessage,
MissingCapability, InvalidDescriptorCount, InvalidDescriptor,
OutOfOrderSequence, UnexpectedReply, SnapshotViolation, LimitExceeded, and
InternalError. Unsupported noncritical messages may continue after an error;
critical or structurally unsafe violations close after a safely queued error.

## Snapshots

`SnapshotBegin` (`0x0010`) contains a nonzero `u64 snapshot_id`, `u16 domain`,
`u16 flags`, `u64 generation`, `u32 expected_count` (`0xffffffff` means
unknown), and zero `u32 reserved`. Domains are Outputs, Surfaces, WindowPolicy,
CompleteSession, and Test.

Items between begin and end carry `SnapshotItem` and increment the tracked
count. `SnapshotEnd` (`0x0011`) contains `u64 snapshot_id`, `u64 generation`,
`u32 actual_count`, and zero `u32 reserved`. `SnapshotAbort` (`0x0012`)
contains `u64 snapshot_id`, `u16 reason`, `u16 detail_length`, zero
`u32 reserved`, and bounded UTF-8 detail.

Nested snapshots, items outside a snapshot, mismatched identity/generation or
counts, and end/abort without begin are violations. Disconnect reports an
active incoming snapshot as aborted.

## Compositor Contract Registry

These messages are consumed by the M4 `gwcomp` process.

`OutputUpsert` (`0x0100`) carries a nonzero 64-bit output ID, enabled state,
signed logical position, logical and physical dimensions, refresh in
millihertz, rational scale, transform, SDR color space/transfer/primaries, and
explicit luminance availability and values. Scale denominator and enabled
dimensions are nonzero; boolean and metadata enums are known.

`OutputRemove` (`0x0101`) carries `u64 output_id`.

`OutputDescriptorUpsert` (`0x0102`) is an Outputs-snapshot item with this
payload:

```text
u64 output_id
u16 kind; u16 name_length
u32 capability_flags
u32 physical_width_millimeters; u32 physical_height_millimeters
u32 supported_transform_mask
u32 minimum_scale_numerator; u32 minimum_scale_denominator
u32 maximum_scale_numerator; u32 maximum_scale_denominator
u32 maximum_scale_denominator_value
u32 maximum_physical_width; u32 maximum_physical_height
u8 name[name_length]
```

Kind is Headless=1 or DRM=2. Capability bits 0 through 6 are Connected,
ArbitraryHeadlessMode, ModeFixed, ScaleConfigurable, TransformConfigurable,
PrimaryEligible, and PhysicalDimensionsKnown. ArbitraryHeadlessMode and
ModeFixed are mutually exclusive, and DRM cannot advertise
ArbitraryHeadlessMode. The nonempty UTF-8 name is at most 63 bytes. A known
physical size requires both millimeter dimensions; otherwise both are zero.
The transform mask is a nonempty subset whose bit number equals the transform
enum value (Normal through Flipped270). Minimum and maximum scales are reduced,
ordered rationals within 1/1 through 4/1 and
the advertised denominator limit, which cannot exceed 120. Maximum physical
dimensions are nonzero and at most 4,096.

`OutputModeUpsert` (`0x0103`) is another Outputs-snapshot item:

```text
u64 output_id; u64 mode_id
u32 physical_width; u32 physical_height; u32 refresh_millihertz
u8 preferred; u8 current; u16 reserved=0
u32 flags=0; u32 reserved=0
```

Both IDs, dimensions, and refresh are nonzero; dimensions and pixel count obey
the output limits, and both booleans are exact zero or one.

`SurfaceUpsert` (`0x0110`) carries surface, optional X11 window, parent, and
output IDs; signed logical position and stacking; logical dimensions; visible
and clipping state plus clip rectangle; transform; unsigned 16.16 opacity;
rational scale; SDR color metadata; presentation flags; and explicit tri-state
fullscreen/direct-scanout eligibility. Dimensions and scale denominator are
nonzero, opacity is at most 1.0, and all booleans/enums are known.
With ScaleAwareSurfaces and ScaleMetadata negotiated, the surface scale is the
integer client-buffer scale from one through four and its denominator is one.
Without that profile historical surfaces remain 1/1; the payload bytes and
message ID are unchanged.

`SurfaceRemove` (`0x0111`) carries `u64 surface_id`.

`SurfaceOutputState` (`0x0113`) is a CompleteSession-snapshot item:

```text
u64 surface_id; u64 primary_output_id; u64 layout_generation
u32 preferred_scale_numerator; u32 preferred_scale_denominator
u32 client_buffer_scale
u16 scale_mode; u16 reserved=0
u32 flags=0; u32 output_count
u64 output_ids[output_count]
```

Scale mode is Legacy=1 or ScaledPixmap=2. IDs are nonzero, membership contains
at most eight unique output IDs, and a nonempty list contains the primary.
The preferred scale is a reduced output scale, client buffer scale is an
integer from one through four, Legacy requires client scale one, and layout
generation is nonzero. The producer orders membership by logical y, logical x,
then output ID. Hidden or offscreen surfaces may retain a primary with an empty
membership list.

`BufferAttach` (`0x0120`) carries buffer and surface IDs, dimensions, stride,
byte offset, storage size, pixel format, modifier, alpha semantics, SDR color
metadata, synchronization mode, and flags. Synchronization None has exactly one
descriptor, the pixel buffer. Synchronization EventFd has exactly two: the
pixel buffer followed by a nonblocking, close-on-exec eventfd. EventFd requires
the API 0.7 CPU-buffer-synchronization capability; the payload layout and wire
version are unchanged. Wire 1.0 accepts linear XRGB8888 or premultiplied
ARGB8888. Checked geometry must fit the declared storage; `fstat` must succeed
and the pixel descriptor must be suitable for mapping.

`BufferDetach` (`0x0121`) carries `u64 surface_id, u64 buffer_id`.
`BufferRelease` (`0x0122`) carries `u64 buffer_id`, `u16 reason`, zero `u16`
and zero `u32` reserved fields.

`SurfaceDamage` (`0x0130`) carries `u64 surface_id`, `u32 rectangle_count`, a
zero `u32 reserved`, then exactly that many `{ i32 x, i32 y, u32 width,
u32 height }` records. The count and rectangle extent arithmetic are bounded;
zero-area rectangles are rejected.

`FrameCommit` (`0x0140`) carries `u64 commit_id`, `u64 output_id` (zero means
all), `u64 producer_generation`, `u32 flags`, and zero `u32 reserved`.

`FrameAcknowledged` (`0x0141`) is a correlated reply carrying `u64 commit_id`,
`u64 output_id`, `u64 presented_generation`, `u16 result`, zero `u16`, and zero
`u32` reserved. Results are Accepted, RejectedIncompleteMetadata,
RejectedInvalidBuffer, RejectedUnknownSurface, and Dropped.

## Window Policy Contract Registry

`PolicyContextUpsert` (`0x0200`) carries `u32 root_window_id`, `u32
workspace_id`, `u64 output_id`, signed work-area x/y, unsigned width/height,
`u32 flags`, and zero `u32 reserved`. IDs and dimensions are nonzero, extents
fit signed coordinates, and flags are zero.

`PolicyWindowUpsert` (`0x0201`) carries window, parent, transient, and workspace
IDs; requested geometry and border width; window type and map intent; exact
boolean and tri-state hints; creation, map, and focus serials; flags; and zero
reserved fields. Width, height, window ID, and creation serial are nonzero.
Mapped intent requires a nonzero map serial; unmapped intent requires zero.
Cross-window references are validated at policy-commit time so snapshot item
order cannot affect validity.

`PolicyWindowRemove` (`0x0202`) carries a nonzero `u32 window_id` and zero
reserved word. `PolicyCommit` (`0x0210`) carries nonzero `u64 commit_id` and
producer generation, `u32 flags`, and zero reserved word. It carries exactly
`AckRequired`, occurs outside an active snapshot, and is tracked for reply
correlation.

`PolicyOutputUpsert` (`0x0204`) is a WindowPolicy-snapshot item:

```text
u64 output_id
i32 logical_x; i32 logical_y; u32 logical_width; u32 logical_height
i32 work_x; i32 work_y; u32 work_width; u32 work_height
u32 scale_numerator; u32 scale_denominator
u16 transform; u8 enabled; u8 primary
u32 flags=0
```

The output ID is nonzero, scale is reduced and within 1/1 through 4/1, the
transform is one of the eight output transforms, and booleans are exact.
Primary implies enabled. An enabled logical/work rectangle is nonnegative,
nonempty, checked, and the work rectangle lies inside the logical rectangle. A
disabled record is nonprimary and has zero logical and work geometry.

`PolicyWindowOutputHint` (`0x0205`) is a WindowPolicy-snapshot item:

```text
u32 window_id; u32 flags=0
u64 previous_output_id; u64 preferred_output_id
```

The window ID is nonzero. Output IDs may be zero where the producer has no
previous or preferred assignment. These output records extend the policy hash
as profile v3; v1 base-policy and v2 interactive-binding bytes do not change.

The canonical v3 base hash is FNV-1a 64-bit over the literal
`glasswyrm-policy-v3`, the existing little-endian generation and context
fields, a little-endian `u32` output count plus output records sorted by output
ID, a little-endian `u32` hint count plus hints sorted by window ID, and the
existing exact 64-byte policy-window records in output order. Output hash
records omit wire padding: ID; logical rectangle; work rectangle; scale;
one-byte transform, enabled, and primary; then flags. Hint hash records are
window, previous output, preferred output, and flags. Interactive v3 hashes use
the same v3 tag around the base v3 hash and the unchanged v2 binding fields.

`PolicyWindowState` (`0x0211`) is an output `SnapshotItem`. It carries identity,
workspace/output, final geometry, signed stacking, window and applied-state
enums, exact visibility/focus/management/decoration/override/attention
booleans, fullscreen and direct-scanout tri-states, and zero flags/reserved.

`PolicyAcknowledged` (`0x0212`) is a `Reply` to one tracked PolicyCommit and
carries matching commit and producer generation, applied generation, policy
hash, window count, and a result from Accepted,
RejectedIncompleteSnapshot, RejectedInvalidContext, RejectedInvalidWindow,
RejectedUnknownReference, RejectedLimit, or RejectedUnsupportedMetadata. A
reply-to mismatch or payload commit-ID mismatch is a protocol error. Policy
messages never carry descriptors.

PolicyContextUpsert and PolicyWindowUpsert use `SnapshotItem` while inside a
snapshot and zero flags for incremental updates. PolicyWindowRemove uses zero
flags. PolicyCommit uses exactly `AckRequired`; PolicyWindowState uses exactly
`SnapshotItem`; PolicyAcknowledged uses exactly `Reply`. Other flag
combinations are protocol errors.

`PolicyBindingsUpsert` (`0x0213`) is one output-snapshot item carrying the
move, resize, and close modifier bindings, pointer buttons, close keysym,
minimum geometry, and raise/consume booleans. It requires InteractivePolicy,
carries no descriptors, and is required exactly once in each negotiated
policy snapshot. Legacy peers retain the v1 policy hash.

## Session State Contract Registry

`SessionStateChange` (`0x0400`) is an AckRequired compositor-to-server request
with nonzero increasing generation, Active or Inactive state, and zero
flags/reserved fields. It requires SessionState and carries no descriptors.

`SessionStateAcknowledged` (`0x0401`) is its correlated Reply. Generation and
state must match and result is Accepted, AlreadyApplied, InputUnavailable, or
Failed. The direction, capability, flags, sequence correlation, and descriptor
count are validated by the connection layer.

CursorSurface does not add message IDs. It capability-gates a cursor-marked
SurfaceUpsert plus ARGB8888 memfd BufferAttach, damage, and frame commit using
the existing compositor registry.

## Output Control Registry

`OutputStateQuery` (`0x0500`) contains:

```text
u64 query_id
u32 flags; u32 reserved=0
```

The nonzero flag mask selects Descriptors (bit 0), Modes (bit 1), Layout (bit
2), and Windows (bit 3). The query carries exactly `AckRequired` outside a
snapshot. A ProtocolServer sends it to a Compositor under OutputManagement; a
DiagnosticTool sends it to the ProtocolServer under OutputControl.

The queried peer answers with an Outputs snapshot containing the requested
`OutputDescriptorUpsert`, `OutputModeUpsert`, and existing `OutputUpsert`
records, followed by `OutputConfigurationAcknowledged` as the correlated
reply. Descriptor and mode inventory flows Compositor to ProtocolServer under
OutputManagement, or ProtocolServer to DiagnosticTool under OutputControl.

`OutputConfigurationCommit` (`0x0501`) contains:

```text
u64 configuration_id; u64 base_generation; u64 primary_output_id
u32 flags=0; u32 reserved=0
```

All three identifiers/generations are nonzero. A same-UID DiagnosticTool sends
an Outputs snapshot containing complete desired `OutputUpsert` states, ends
the snapshot, then sends this message with exactly `AckRequired`. The commit is
valid only DiagnosticTool to ProtocolServer with OutputControl and outside an
active snapshot.

Only one output-configuration transaction may be active. Read-only queries may
continue while no transaction result is being serialized; Busy rejects a
competing configuration without partially applying it.

`OutputConfigurationAcknowledged` (`0x0502`) contains:

```text
u64 request_id; u64 applied_generation
u16 result; u16 reserved=0; u32 flags=0
u64 primary_output_id
u32 root_logical_width; u32 root_logical_height
u32 enabled_output_count; u32 reserved=0
```

Result values 1 through 12 are Accepted, StaleGeneration, Busy, InvalidLayout,
UnknownOutput, UnsupportedMode, UnsupportedScale, UnsupportedTransform,
PolicyRejected, CompositorRejected, PresenterRejected, and InternalError. The
request ID, applied generation, and primary output are nonzero; the root is
nonempty and within 32,767 by 32,767; enabled count is one through eight.

The acknowledgement carries exactly `Reply` outside a snapshot. It flows
Compositor to ProtocolServer under OutputManagement, or ProtocolServer to
DiagnosticTool under OutputControl. `reply_to` must name a tracked
OutputStateQuery or OutputConfigurationCommit sequence, and payload
`request_id` must equal that query's `query_id` or commit's
`configuration_id`. Outgoing and incoming tracking are bounded by the peer's
message queue limit. A sequence or payload-identity mismatch is an unexpected
reply protocol error on that connection; it does not disturb unrelated peers.

The remaining API 0.8 directions and flag rules are fixed:

| Message | Direction | Required negotiated capabilities | Envelope/snapshot rule |
|---|---|---|---|
| OutputDescriptorUpsert, OutputModeUpsert | Compositor to ProtocolServer, or ProtocolServer to DiagnosticTool | OutputManagement, or OutputControl | exactly `SnapshotItem` in Outputs |
| SurfaceOutputState | ProtocolServer to Compositor | SurfaceOutputMembership and ScaleMetadata | exactly `SnapshotItem` in CompleteSession |
| PolicyOutputUpsert, PolicyWindowOutputHint | ProtocolServer to WindowManager | WindowPolicy, MultiOutputPolicy, and ScaleMetadata | exactly `SnapshotItem` in WindowPolicy |
| OutputStateQuery | ProtocolServer to Compositor, or DiagnosticTool to ProtocolServer | OutputManagement, or OutputControl | exactly `AckRequired`, outside a snapshot |
| OutputConfigurationCommit | DiagnosticTool to ProtocolServer | OutputControl | exactly `AckRequired`, outside a snapshot |
| OutputConfigurationAcknowledged | reverse of the tracked query or commit | OutputManagement, or OutputControl | exactly `Reply`, outside a snapshot |

All eight API 0.8 message types require an FD count of zero. A descriptor on
any of them is a protocol error even when FdPassing was negotiated. Output
control uses a separate same-UID listener, accepts only DiagnosticTool peers,
and does not relax the early local trust model.

## Descriptor Ownership

The sender queue owns close-on-exec duplicates and closes them after send or
queue teardown. Receive uses `MSG_CMSG_CLOEXEC`; every received descriptor is
owned by its message until explicitly taken. Ancillary or payload truncation,
unexpected control messages, count mismatch, and per-message descriptor-rule
violations close every received descriptor and reject the record.

## Proof

Golden and malformed codec coverage runs with the normal Meson suite:

```sh
meson test -C build --print-errorlogs gwipc-envelope gwipc-control-codec \
  gwipc-snapshot-codec gwipc-compositor-contract gwipc-malformed \
  gwipc-policy-contract gwipc-public-control-api gwipc-protocol-edges \
  gwipc-output-contract gwipc-public-output-api gwipc-output-protocol
```
