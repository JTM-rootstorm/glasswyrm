# 0004: Add Headless Core X11 Resource State

Status: Accepted

## Context

Milestone 1 ends after X11 11.0 connection setup. Milestone 2 needs enough
ordinary core protocol behavior to exercise request framing, resource lifetime,
atoms, and typed properties without implying that windows can be mapped,
displayed, focused, or sent to another Glasswyrm process.

The server must remain robust when clients fragment or pipeline requests, use
either X11 byte order, stop reading replies, send recoverable protocol errors,
or disconnect while they still own resources.

## Decision

`glasswyrmd` remains the sole protocol and resource authority. It gains a
server-owned state model containing the immutable synthetic screen, a typed
window resource table, a global atom table, and property-memory accounting.
Each connection owns only its transport state, request sequence, assigned
resource range, and an identity used for cleanup. `gwm` and `gwcomp` remain
placeholders and receive no notification.

The supported ordinary requests are exactly `CreateWindow`, `DestroyWindow`,
`GetGeometry`, `QueryTree`, `InternAtom`, `GetAtomName`, `ChangeProperty`,
`DeleteProperty`, `GetProperty`, `ListProperties`, a synthetic
`GetInputFocus`, and `NoOperation`. Other core opcodes return `BadRequest` and
do not close the connection.

Ordinary requests use the four-byte X11 header and are bounded by the setup
reply's 65,535 four-byte-unit maximum. Length zero and larger declarations
produce `BadLength` and close because framing cannot safely advance. Complete
request-specific length errors produce `BadLength` and leave the stream usable.
Each complete request advances a 64-bit connection sequence; replies and errors
carry its low 16 bits. A connection processes at most 64 requests or 256 KiB of
request bytes per reactor turn.

Output is an ordered per-client FIFO with partial-write support and a 1 MiB
queued-byte cap. Setup output always precedes request replies and errors. A
client that exceeds the output cap is closed without affecting other clients.

The synthetic screen constants are shared by setup and request handling. The
root window is a permanent server-owned depth-24 window. Client-created XIDs
must be nonzero, match the connection's advertised base and mask, avoid all
server-owned IDs, and be unused globally. Client windows are always unmapped
records. Parent and ordered child links are maintained in both directions;
destruction is recursive, including descendants owned by other clients.
Disconnect uses DestroyAll behavior for every resource still owned by that
client, while preserving the root and maintaining all owner indexes.

The atom table contains all core predefined atoms with their protocol IDs.
Dynamic atoms are case-sensitive, global for the server lifetime, monotonic,
and persist across client disconnects.

Property values use canonical typed storage: raw bytes for format 8, host-side
16-bit units for format 16, and host-side 32-bit units for format 32. Values
are decoded from the writer's byte order and encoded in the reader's byte
order. A property is capped at 4 MiB, total server property data at 64 MiB,
and each window at 4,096 properties. Cap failures return `BadAlloc` without
partially mutating state.

## Consequences

- Raw and libxcb probes can create, query, and destroy unmapped window records.
- Clients using opposite byte orders share atoms and typed properties safely.
- Recoverable errors preserve request ordering and connection usability.
- Resource and property cleanup is deterministic and directly testable.
- No events, mapping, input, focus policy, WM/compositor IPC, rendering,
  framebuffer, or display output exists in this milestone.
- The local unauthenticated transport remains unsuitable for a real multi-user
  desktop.
