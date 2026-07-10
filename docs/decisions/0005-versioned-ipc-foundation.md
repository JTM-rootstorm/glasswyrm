# 0005: Establish a Versioned Local IPC Foundation

Status: Accepted

## Context

Glasswyrm's protocol server, window manager, and compositor have distinct
authority. They need an explicit process boundary before any runtime component
can exchange compositor-facing state. That boundary must preserve message and
descriptor association, remain testable without display hardware, and avoid
coupling internal contracts to X11 byte order or C++ implementation details.

## Decision

Milestone 3 adds `libgwipc` as an independently buildable shared library with
an opaque C ABI and thin, move-only C++ RAII wrappers. The library has API
version 0.1.0 and SOVERSION 0. Its wire protocol is versioned independently and
starts at 1.0.

Connections use nonblocking, close-on-exec Linux `AF_UNIX` `SOCK_SEQPACKET`
sockets at explicit filesystem paths. Listener directories and sockets default
to modes 0700 and 0600. Stale endpoint replacement uses ownership, liveness,
and inode checks; destruction removes only the socket created by that listener.
Peers are identified with `SO_PEERCRED` and must have the effective user's UID
unless an explicit test policy overrides that rule.

Every record has a fixed 40-byte envelope and explicit little-endian fields.
Native structures are never serialized. The handshake negotiates wire version,
roles, capabilities, payload size, and descriptor count before application
messages can be delivered. Per-direction sequences, reply correlation,
capability gates, snapshot state, and bounded queues are validated by the
library. Malformed peers are isolated from other connections.

Queued file descriptors are close-on-exec duplicates owned by the connection.
Received messages own their descriptors until one is explicitly taken; message
destruction closes every unclaimed descriptor. Ancillary truncation and
descriptor-count mismatches are protocol failures.

The initial vocabulary describes outputs, surfaces, buffers, damage, frames,
and snapshot boundaries. These are contracts only: `glasswyrmd`, `gwm`, and
`gwcomp` do not link to or use `libgwipc` in Milestone 3.

## Consequences

- IPC codecs can be tested without sockets or display hardware.
- Callers drive connections through `poll()`; the library has no hidden event
  loop, threads, callbacks, or global connection registry.
- Same-UID local IPC is an early trust boundary, not process isolation.
- SOVERSION 0 permits deliberate ABI iteration, while wire compatibility is
  governed separately by major/minor negotiation.
- Runtime authority remains unchanged until a later milestone integrates the
  three Glasswyrm processes through these contracts.
