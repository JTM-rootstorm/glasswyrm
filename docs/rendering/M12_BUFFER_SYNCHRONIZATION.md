# Milestone 12 CPU-buffer synchronization

GWIPC API 0.7 adds an opt-in eventfd handoff for CPU-written shared buffers.
It closes the ordering gap between `glasswyrmd` finishing a canonical-to-memfd
copy and `gwcomp` reading the mapped pixels. Wire version 1.0 and SOVERSION 0
remain unchanged.

## Negotiation and descriptors

Both peers must negotiate `GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION`. A
`BufferAttach` with `GWIPC_SYNCHRONIZATION_NONE` carries exactly one descriptor,
the pixel memfd. `GWIPC_SYNCHRONIZATION_EVENTFD` carries exactly two descriptors
in order: the pixel memfd and a nonblocking, close-on-exec eventfd.

The compositor rejects mismatched descriptor counts, unsupported synchronization
values, or an eventfd without `O_NONBLOCK` and `FD_CLOEXEC`. Historical peers
continue to use `None` and retain their one-descriptor contract.

## Producer sequence

Each published buffer owns its eventfd for the buffer lifetime. For one frame,
`glasswyrmd`:

1. copies normalized dirty rows from canonical pixels into the memfd mapping;
2. completes the existing mapping synchronization;
3. issues a release fence;
4. writes exactly one eventfd token;
5. submits damage and the acknowledgement-required frame commit.

The server never mutates an in-flight buffer. Replacement and replay allocate
fresh buffer IDs and eventfds. If submission is cancelled before the frame is
observable, the producer retracts its one token; semantic rejection after
consumption follows normal damage rollback instead.

## Consumer sequence

Before rendering a frame, `gwcomp` gathers the distinct synchronized buffers
used by damaged surfaces. It consumes one token from each eventfd and then
issues an acquire fence. Only after every token is present may a renderer read
mapped pixels.

An unavailable token leaves the frame staged on the current eventfd poll
source. It causes no rendering, scene promotion, frame acknowledgement, or
buffer release. The wait shares the bounded presentation deadline. Timeout,
descriptor failure, or an eventfd read whose aggregate value is not exactly one
is producer-protocol fatal; it does not corrupt or stop unrelated peers.

## Ownership and recovery

`FrameAcknowledged` remains the producer ownership-release point. Compositor
disconnect closes its eventfd duplicates, aborts any pending readiness wait,
and causes the server to merge in-flight damage back into pending damage.
Reconnect performs a complete buffer replay with fresh readiness state.

Unit tests cover descriptor validation, would-block and timeout behavior,
multi-buffer waits, acquire ordering, extra-token rejection, cancellation,
disconnect rollback, and API 0.1 through 0.7 staged consumer compatibility.
