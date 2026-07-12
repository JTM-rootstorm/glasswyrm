# Milestone 5 Policy Service

## Scope

`gwm` is a foreground, single-threaded window-policy process. It accepts one
repository-owned synthetic peer and evaluates top-level window metadata. It
does not open an X11 display, connect to `gwcomp`, access DRM or input devices,
render decorations, or deliver events.

## Command line

```text
gwm --ipc-socket PATH [--once] [--max-commits N] [--help] [--version]
```

`--ipc-socket` is required and has no implicit default. `--once` exits after at
least one accepted commit and the active peer disconnects. `--max-commits N`
counts accepted commits only and exits after the response for commit `N` has
been flushed. `SIGINT` and `SIGTERM` wake the poll reactor through a nonblocking
self-pipe and cause clean socket removal.

## Roles and capabilities

`gwm` listens with role `WindowManager`. The only accepted peer role is
`ProtocolServer`. Both sides must negotiate:

```text
Snapshots | WindowPolicy
```

The endpoint is a filesystem Unix `SOCK_SEQPACKET` socket managed by
`libgwipc`. Same-UID credentials are required. Policy messages carry no file
descriptors. While a peer is active, the listener is not polled for another
accept; after disconnect, a new peer may bootstrap.

## Bootstrap and updates

The first accepted policy requires this sequence:

```text
SnapshotBegin domain=WindowPolicy
PolicyContextUpsert SnapshotItem
PolicyWindowUpsert SnapshotItem ...
SnapshotEnd
PolicyCommit AckRequired
```

Exactly one context is required. Zero windows are valid. After bootstrap,
complete context or window records may be upserted, and windows may be removed,
before a later commit. Partial field masks are not supported.

An accepted commit queues:

```text
SnapshotBegin domain=WindowPolicy
PolicyWindowState SnapshotItem ...
SnapshotEnd
PolicyAcknowledged Reply
```

The acknowledgement is correlated to the incoming commit sequence. It reports
the commit and producer generations, applied generation, policy hash, result,
and window count. A rejected commit queues only `PolicyAcknowledged`, reporting
the previously committed generation, hash, and count.

## Reactor and limits

The process uses `poll()` over the listener, active connection, and signal
pipe. Each turn consumes at most 64 messages and 512 KiB of payload. Policy
state is limited to 4,096 windows. Work-area and managed-window dimensions are
bounded at 16,384; border width is bounded at 65,535. Geometry arithmetic is
checked before evaluation.

The outgoing full snapshot is encoded and checked against the configured
queue bounds before state is accepted. If output construction or enqueueing
cannot complete, the peer is closed and its policy state is cleared rather
than exposing a partial completed snapshot.

## Reconnect and isolation

Disconnect clears pending raw state, committed raw state, committed policy,
snapshot tracking, and per-connection commit/generation tracking. The next peer
must send a complete bootstrap snapshot. Wrong-role, missing-capability, and
malformed peers are isolated; a later valid peer can connect.

## Tests

Typical host validation is:

```sh
meson setup build-gwm-only --wipe \
  -Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false \
  -Dgwm=true -Dgwcomp=false -Dtools=false
meson compile -C build-gwm-only
meson test -C build-gwm-only --print-errorlogs
```

The tests cover policy algorithms, public contracts, process lifecycle,
handshake isolation, descriptor churn, exact response ordering, synthetic
scenarios, and deterministic JSON output. Unix-socket process tests may need to
run outside restricted sandboxes that prohibit local IPC.
