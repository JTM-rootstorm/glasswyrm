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

Unknown offered bits are ignored. Unknown required bits reject the handshake.
Negotiated capabilities are the offered intersection and must contain the
requirements of both peers. Buffer attachment requires FdPassing and
MemfdBuffers; snapshots, damage, and frame acknowledgements require their
corresponding capabilities.
Every policy message requires WindowPolicy and carries no descriptors.

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

`SurfaceUpsert` (`0x0110`) carries surface, optional X11 window, parent, and
output IDs; signed logical position and stacking; logical dimensions; visible
and clipping state plus clip rectangle; transform; unsigned 16.16 opacity;
rational scale; SDR color metadata; presentation flags; and explicit tri-state
fullscreen/direct-scanout eligibility. Dimensions and scale denominator are
nonzero, opacity is at most 1.0, and all booleans/enums are known.

`SurfaceRemove` (`0x0111`) carries `u64 surface_id`.

`BufferAttach` (`0x0120`) carries buffer and surface IDs, dimensions, stride,
byte offset, storage size, pixel format, modifier, alpha semantics, SDR color
metadata, synchronization mode, and flags. It has exactly one descriptor.
Wire 1.0 accepts linear XRGB8888 or premultiplied ARGB8888 with no explicit
synchronization. Checked geometry must fit the declared storage; `fstat` must
succeed and the descriptor must be suitable for mapping. M3 validates but does
not map it.

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
  gwipc-policy-contract gwipc-public-control-api gwipc-protocol-edges
```
