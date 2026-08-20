# 0018: Serialize Rust Server State and Snapshot Event Recipients

Status: Accepted for WP0B

## Context

Rust `glasswyrmd` began as one thread per client around a shared mutex-protected
server state. Each client also owned its output queue and the request loop wrote
directly to a Linux `UnixStream`. That was sufficient for early setup and
single-client request parity, but it cannot safely grow into cross-client event
routing, lifecycle peer waits, and restart/replay.

WP0B requires the execution model to be chosen before WP2 expands authoritative
state. It also requires protocol behavior to remain testable without Unix
sockets or Linux devices.

## Decision

Canonical server state has one serialized mutation domain. A mutation completes
under that domain and produces event/publication intents; it never performs
socket I/O, GWIPC peer waits, or blocking cleanup while holding the state lock.
For cross-client delivery, the serialized domain takes one recipient snapshot.
Each snapshot entry contains:

- stable client identity;
- negotiated X11 byte order;
- the recipient sequence reserved for that delivery; and
- a stable queue generation identifying the intended bounded output endpoint.

The state lock is released before enqueue or transport work. If a client dies
between snapshot and delivery, that recipient alone resolves to disconnected.
If its queue exceeds either the message or byte bound, that client is
terminated deterministically and retained queued bytes are discarded. A slow
client cannot block unrelated recipients.

The current WP0B prototype lives in `crates/glasswyrmd/src/execution.rs` and is
transport-free. It proves two-client mixed-byte-order fanout, disconnect during
delivery, queue saturation, sequence reservation, and deterministic cleanup.
WP3 must extend this model rather than creating an independent router.

The X11 client session is likewise byte-driven. Setup and established request
processing live in `ClientSession`; Linux listener paths, credentials, socket
flags, read deadlines, shutdown, and polling remain adapter responsibilities.
GWIPC follows the same separation. Handshake validation consumes the
transport-neutral `RecordSource` contract and semantic attachment counts rather
than `OwnedFd`. Linux `SOCK_SEQPACKET`, ancillary parsing, descriptor ownership,
and credential checks remain in `gw-ipc` transport/endpoint adapters.

Semantic capability names are `HANDLE_ATTACHMENTS` and
`SHARED_BUFFER_OBJECTS`; Linux GWIPC v1 retains `FD_PASSING` and
`MEMFD_BUFFERS` as numeric compatibility aliases.

## Ordering domains

X11 request sequence and recipient event sequence belong to protocol-server
state. GWIPC connection sequences remain per-peer protocol state. A future
lifecycle transaction may correlate these domains, but it may not silently
merge their counters or hold the canonical state lock while waiting for peer
acknowledgements.

WP4 decides whether the existing acknowledgement vocabulary is sufficient for
lifecycle atomicity. This ADR does not authorize a GWIPC wire-version change.

## Revisit when

Revisit only if deterministic WP3 routing cannot be expressed with bounded
recipient snapshots, or if measured contention requires a different serialized
executor while preserving the same observable ordering and lock/I/O rules.
