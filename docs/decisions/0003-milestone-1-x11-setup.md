# 0003: Implement a Bounded Local X11 Setup Service

Status: Accepted

## Context

Milestone 1 needs enough X11 protocol support for a client to negotiate the
initial connection setup without pulling normal request dispatch, resources,
window management, composition, or hardware access into the milestone. The
setup exchange is byte-order-sensitive, permits padded variable-length
authorization fields, and arrives over a stream that may split at any byte.

The milestone also needs deterministic multi-client behavior and reliable
headless tests. It is not yet suitable for a real multi-user desktop because it
has no authorization backend.

## Decision

`glasswyrmd` runs in the foreground and listens only on a filesystem Unix
socket named `X<N>` beneath a configurable socket directory. It uses a small
single-threaded `poll()` reactor with nonblocking, close-on-exec listener and
client descriptors. The process owns protocol truth; `gwm` and `gwcomp` remain
independent placeholders and are not contacted during this milestone.

The setup codec is independent of the socket server. It uses explicit
endian-aware byte readers and writers, checked size arithmetic, bounded buffers,
and an incremental parser instead of packed structures or unaligned casts. It
accepts exactly X11 protocol 11.0 in either client byte order and only when both
authorization fields are empty. A fully framed unsupported version or supplied
authorization value receives a setup-failed reply. Invalid byte-order markers,
oversized frames, arithmetic failures, and truncated input are logged and
closed without attempting an unsafe reply.

Successful clients receive a deterministic synthetic one-screen description:
1024 by 768 pixels, one depth-24 TrueColor visual, one depth-24/32-bpp pixmap
format, conventional RGB masks, and a client-specific non-overlapping resource
ID base. Server-owned root, colormap, and visual identifiers are outside every
client allocation range. Normal X11 requests remain unsupported; sending bytes
after setup closes only that client.

Socket cleanup records the bound filesystem object and removes it only when it
is still the socket created by this process. A live socket is never replaced.
A stale socket is removed only after a failed connection check identifies it as
unowned. Signal handlers only record shutdown intent; cleanup occurs in normal
control flow. The daemon stays foreground and init-system neutral so tests and
either supported Gentoo init system can supervise it directly.

Stale-socket recovery requires the socket inode to be owned by the daemon's
effective user and rechecks its identity immediately before removal. Milestone
1 does not attempt to defend against a hostile same-UID process racing pathname
replacement; same-UID clients already share the unauthenticated local trust
domain in this milestone.

## Consequences

- Raw little- and big-endian clients can complete X11 11.0 setup.
- A libxcb client may connect, inspect the setup record, and disconnect without
  issuing a normal request.
- Fragmented, malformed, and concurrent clients can be tested without hardware.
- `MIT-MAGIC-COOKIE-1` and every other authorization method are unsupported.
- Accepting unauthenticated local clients is a research limitation, not a
  security boundary.
- TCP, abstract sockets, request dispatch, resources, events, input, IPC,
  rendering, and display output remain outside Milestone 1.
