# GWIPC API 0

## Version And ABI

The installed C API is version 0.8.0 and the shared library has SOVERSION 0.
`gwipc_get_api_version()` reports the library API version;
`gwipc_get_max_wire_version()` independently reports the highest supported wire
version. ABI compatibility must not be inferred from wire compatibility.

All exported C declarations are available through `<glasswyrm/ipc.h>`. Public
listener, connection, and message handles are opaque. Options and outgoing
message structures begin with `struct_size`; callers initialize unused and
reserved fields to zero. Status-returning functions do not throw exceptions.
System failures expose the saved `errno` through the owning listener or
connection.

API 0 remains additive. API 0.1 transport consumers, API 0.2 compositor
contract consumers, and API 0.3 policy consumers retain their symbols and wire
encodings. API 0.4 adds lifecycle records and sequence-returning enqueue;
API 0.5 adds synthetic-input contracts; API 0.6 adds session-state and
interactive-policy contracts plus cursor-surface capability negotiation; and
API 0.7 adds capability-gated eventfd CPU-buffer synchronization. API 0.8 adds
output inventory, surface-output membership, multi-output policy, scale-aware
surface negotiation, and output-control contracts.
Wire 1.0 remains unchanged.

## Installed Consumer Matrix

`tests/install/gwipc_staged_consumers_test.sh` installs the selected build into
an isolated `DESTDIR`, resolves `gwipc` only through that staged `pkg-config`
file, and compiles, links, and runs C and C++ consumers for every additive API
generation from 0.1 through 0.8. Run it after compiling the build tree:

```sh
tests/install/gwipc_staged_consumers_test.sh "$PWD" "$PWD/build-m13"
```

The Milestone 11 VM acceptance invoked the then-current matrix before declaring
its API consumer result passed; Milestone 13 invokes the expanded 0.1-through-0.8
matrix. The consumer sources intentionally use only the API surface available
in their named generation; no source-tree include path or build-tree library
path is accepted by the staged runner.

The runner also reads the selected build's `tools` option. A tools-enabled
build must install `gwinfo` and `gwout`, and their installed copies must
complete both `--help` and `--version` using the staged `libgwipc`. A
tools-disabled build must install neither executable. This keeps the smoke
test on the installed boundary rather than accidentally exercising build-tree
binaries.

| API | C consumer | C++ consumer | Additive surface exercised |
|---:|---|---|---|
| 0.1 | `gwipc_transport_c_consumer.c` | `gwipc_transport_cpp_consumer.cpp` | transport and opaque handles |
| 0.2 | `gwipc_c_consumer.c` | `gwipc_cpp_consumer.cpp` | compositor contracts |
| 0.3 | `gwipc_policy_c_consumer.c` | `gwipc_policy_cpp_consumer.cpp` | snapshots and window policy |
| 0.4 | `gwipc_lifecycle_c_consumer.c` | `gwipc_lifecycle_cpp_consumer.cpp` | lifecycle records and sequence enqueue |
| 0.5 | `gwipc_input_c_consumer.c` | `gwipc_input_cpp_consumer.cpp` | synthetic input |
| 0.6 | `gwipc_session_c_consumer.c` | `gwipc_session_cpp_consumer.cpp` | session, interactive policy, and cursor capability |
| 0.7 | `gwipc_sync_c_consumer.c` | `gwipc_sync_cpp_consumer.cpp` | eventfd CPU-buffer synchronization |
| 0.8 | `gwipc_output_c_consumer.c` | `gwipc_output_cpp_consumer.cpp` | output inventory, membership, policy, and control |

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

`<glasswyrm/ipc/input.h>` contains API 0.5 synthetic-input records.
`<glasswyrm/ipc/session.h>` adds API 0.6 `SessionStateChange` and correlated
`SessionStateAcknowledged` records. API 0.6 also extends
`<glasswyrm/ipc/policy.h>` with `PolicyBindingsUpsert`. The new capability bits
are SessionState, InteractivePolicy, and CursorSurface; cursor publication
reuses the existing surface, buffer, damage, and frame contracts.

API 0.7 adds `GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION` and
`GWIPC_SYNCHRONIZATION_EVENTFD` without changing the BufferAttach payload.
Synchronization-none attachments retain one pixel-buffer descriptor. Eventfd
attachments carry the pixel buffer followed by a nonblocking, close-on-exec
eventfd. The eventfd token is consumed before the compositor reads damaged
pixels; FrameAcknowledged remains the producer ownership-release point.

API 0.8 installs `<glasswyrm/ipc/output.h>` and adds these capability bits:

| Bit | Public capability |
|---:|---|
| 17 | `GWIPC_CAP_OUTPUT_MANAGEMENT` |
| 18 | `GWIPC_CAP_MULTI_OUTPUT_POLICY` |
| 19 | `GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP` |
| 20 | `GWIPC_CAP_SCALE_AWARE_SURFACES` |
| 21 | `GWIPC_CAP_OUTPUT_CONTROL` |

The additive message registry is:

| ID | Public message constant | Public record |
|---:|---|---|
| `0x0102` | `GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT` | `gwipc_output_descriptor_upsert` |
| `0x0103` | `GWIPC_MESSAGE_OUTPUT_MODE_UPSERT` | `gwipc_output_mode_upsert` |
| `0x0113` | `GWIPC_MESSAGE_SURFACE_OUTPUT_STATE` | `gwipc_surface_output_state` |
| `0x0204` | `GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT` | `gwipc_policy_output_upsert` |
| `0x0205` | `GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT` | `gwipc_policy_window_output_hint` |
| `0x0500` | `GWIPC_MESSAGE_OUTPUT_STATE_QUERY` | `gwipc_output_state_query` |
| `0x0501` | `GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT` | `gwipc_output_configuration_commit` |
| `0x0502` | `GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED` | `gwipc_output_configuration_acknowledged` |

`GWIPC_CAP_SCALE_METADATA` remains required alongside the applicable M13
membership, policy, and scale-aware behavior. The new header exposes bounded
output descriptors and modes, per-surface membership, per-output policy state,
window output hints, output-state queries, configuration commits, and one
correlated acknowledgement type shared by queries and commits. It also exposes
the fixed M13 limits: eight outputs, 63 output-name bytes, 128 modes per output,
4,096 pixels per physical dimension, 16,777,216 pixels per output, 67,108,864
total output pixels, a 32,767 by 32,767 maximum logical root, rational output
scale from 1/1 through 4/1 with denominator at most 120, and eight active
output-control peers.

Descriptor capability flags are Connected, ArbitraryHeadlessMode, ModeFixed,
ScaleConfigurable, TransformConfigurable, PrimaryEligible, and
PhysicalDimensionsKnown, exposed as the `GWIPC_OUTPUT_CAP_*` bits. Query flags
independently request Descriptors, Modes, Layout, and Windows through
`GWIPC_OUTPUT_QUERY_*`. Surface scale mode is
`GWIPC_SURFACE_SCALE_LEGACY` or `GWIPC_SURFACE_SCALE_SCALED_PIXMAP`.
`gwipc_output_configuration_result` contains Accepted, StaleGeneration, Busy,
InvalidLayout, UnknownOutput, UnsupportedMode, UnsupportedScale,
UnsupportedTransform, PolicyRejected, CompositorRejected, PresenterRejected,
and InternalError.

Every API 0.8 record has an encoder and a decoded accessor using the existing
`gwipc_contract_payload` and `gwipc_decoded_contract` ownership model. Variable
descriptor names and surface membership arrays are copied into encoded payloads;
decoded pointers remain owned by the decoded-contract object. The public
`struct_size` and reserved-zero conventions remain unchanged.
These new encode/accessor symbols are assigned to `GWIPC_0.8`; symbols from
`GWIPC_0.1` through `GWIPC_0.7` retain their existing symbol versions.

OutputStateQuery and OutputConfigurationCommit carry `AckRequired`. The library
tracks their outgoing sequence together with the nonzero query or configuration
ID. OutputConfigurationAcknowledged carries `Reply`; both its `reply_to`
sequence and payload `request_id` must match the tracked request. A mismatch is
a protocol error on that connection and does not invalidate unrelated peers.
All API 0.8 output-management and output-control messages carry zero file
descriptors.

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

`gwipc_connection_enqueue_with_sequence()` has the same ownership and atomic
failure behavior as the original enqueue function and reports the assigned
outgoing sequence for exact reply correlation.

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
