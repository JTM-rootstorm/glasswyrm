# GWIPC API 0

## Version And ABI

The installed C API is version 0.3.0 and the shared library has SOVERSION 0.
`gwipc_get_api_version()` reports the library API version;
`gwipc_get_max_wire_version()` independently reports the highest supported wire
version. ABI compatibility must not be inferred from wire compatibility.

All exported C declarations are available through `<glasswyrm/ipc.h>`. Public
listener, connection, and message handles are opaque. Options and outgoing
message structures begin with `struct_size`; callers initialize unused and
reserved fields to zero. Status-returning functions do not throw exceptions.
System failures expose the saved `errno` through the owning listener or
connection.

API 0 remains additive. API 0.1 transport consumers and API 0.2 compositor
contract consumers retain their symbols and wire encodings. API 0.3 adds public
snapshot-control and window-policy structures without changing wire 1.0.

## Typed Control And Contracts

`<glasswyrm/ipc/control.h>` exposes owned encodings and decoded views for
`SnapshotBegin`, `SnapshotEnd`, and `SnapshotAbort`. Callers destroy encoded
payloads with `gwipc_control_payload_destroy()` and decoded controls with
`gwipc_decoded_control_destroy()`. Decoded abort detail remains owned by the
decoded-control object.

`<glasswyrm/ipc/contracts.h>` contains the compositor vocabulary introduced in
API 0.2. `<glasswyrm/ipc/policy.h>` adds API 0.3 structures and encode/decode
accessors for policy context, raw-window updates and removal, policy commits,
evaluated window state, and correlated acknowledgements. Contract payloads and
decoded contracts use the existing API 0.2 ownership functions.

All public input structures begin with `struct_size` and end with reserved-zero
storage. Boolean, enum, identifier, dimension, flag, and serial constraints are
validated before an encoding is returned. Public decoders reject truncated or
trailing payload data.

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
