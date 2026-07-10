# GWIPC API 0

## Version And ABI

The installed C ABI is version 0.1.0 and the shared library has SOVERSION 0.
`gwipc_get_api_version()` reports the library API version;
`gwipc_get_max_wire_version()` independently reports the highest supported wire
version. ABI compatibility must not be inferred from wire compatibility.

All exported C declarations are available through `<glasswyrm/ipc.h>`. Public
listener, connection, and message handles are opaque. Options and outgoing
message structures begin with `struct_size`; callers initialize unused and
reserved fields to zero. Status-returning functions do not throw exceptions.
System failures expose the saved `errno` through the owning listener or
connection.

## Object Lifetimes

`gwipc_listener_create()` owns a filesystem socket until
`gwipc_listener_destroy()`. Accepted and connected `gwipc_connection` objects
are independent of the listener and must be destroyed explicitly.

`gwipc_connection_receive()` transfers a received `gwipc_message` to the
caller. Destroying it closes every attached descriptor that has not been taken.
`gwipc_message_take_fd()` transfers one descriptor to the caller exactly once.

Enqueueing a message duplicates every supplied descriptor with close-on-exec.
The caller retains the originals. The connection owns its duplicates until
send completion, cancellation, failure, or destruction.

## Nonblocking Operation

Sockets are nonblocking and close-on-exec. The library owns no event loop.
Callers include `gwipc_connection_fd()` in their poll set using the events from
`gwipc_connection_wanted_poll_events()`, then pass returned events to
`gwipc_connection_process_poll_events()`. `WouldBlock` and `InProgress` are
ordinary flow-control results. Application messages are available only after
the connection reaches `Established`.

Queue byte, message, payload, and descriptor limits are bounded. A peer that
violates a negotiated protocol or limit is closed without affecting other
connections.

## C++ Wrappers

`<glasswyrm/ipc.hpp>` provides move-only, `noexcept` RAII wrappers for
`Listener`, `Connection`, `Message`, and `OwnedFd`. They delegate to the C ABI
and contain no separate protocol implementation.

## Thread Safety

Each object is not internally thread-safe; callers serialize access to it.
Distinct objects may be used on distinct threads. The library creates no hidden
threads and invokes no callbacks.

## Security Scope

Endpoints are local filesystem Unix sockets. Peer credentials come from the
kernel, and same-UID peers are required by default. This early local trust model
does not provide client isolation comparable to a mature display stack.
