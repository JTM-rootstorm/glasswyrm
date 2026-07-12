# Milestone 4 Headless Compositor

## Scope and status

Milestone 4 is the compositor-facing proof of Glasswyrm's three-process
architecture. It does not implement mapped X11 windows, WM policy, input,
DRM/KMS, scaling, HDR, VRR, or accelerated rendering.

Implemented today are bounded geometry/damage, staged scene, and read-only
memfd mapping primitives, a software renderer, a bounded headless framebuffer,
atomic PPM writing, CLI parsing, and a foreground `gwcomp` GWIPC listener. The
listener accepts one negotiated producer and survives disconnects, but it does not yet dispatch
scene or buffer messages. Frame commits currently receive
`RejectedIncompleteMetadata`.

## Command line

```text
gwcomp --ipc-socket PATH --dump-dir PATH [--once] [--max-frames N]
       [--help] [--version]
```

Both paths are required. The dump directory must not be a symbolic link.
`--max-frames` requires a positive integer. The parser accepts `--once` and
`--max-frames`; their successful-frame exit behavior is not wired into the
reactor yet.

## Topology and lifecycle

`gwcomp` listens on a local `AF_UNIX` `SOCK_SEQPACKET` endpoint as role
`Compositor` and accepts at most one role `TestProducer`. A self-pipe wakes its
poll loop for `SIGINT` and `SIGTERM`. A disconnected producer is discarded and
the listener remains available for a new connection. Scene cleanup on
disconnect is implemented in the scene model but not yet connected to the
reactor because the reactor does not own a scene instance yet.

The required capability set is:

- descriptor passing and memfd buffers;
- snapshots;
- output and surface state;
- damage regions;
- SDR color metadata;
- frame acknowledgements.

The intended bootstrap is one `CompleteSession` snapshot containing the full
output, surface, and buffer state. Incremental mutations and commits before a
complete snapshot are rejected by the scene model.

## Intended supported contract subset

The M4 scene path is limited to output upsert/removal, surface upsert/removal,
surface damage, buffer attach/release, frame commit/acknowledgement, and
complete-session snapshot boundaries. Output and surface metadata codecs
already exist. A validated read-only memfd mapping primitive also exists;
reactor dispatch, buffer attachment lifetime, and release handling remain to be
completed.

## Bounds

- one output, with a nonzero identifier;
- maximum output extent: 4096 by 4096 pixels;
- maximum output pixels: 16,777,216 (64 MiB at four bytes per pixel);
- maximum 4096 surfaces;
- damage rectangle counts are bounded by the GWIPC wire contract;
- 64 received messages and 512 KiB of payload are processed per reactor turn.

Output transforms, scaling, parent surfaces, and non-SDR metadata are outside
this milestone.

## Commit and acknowledgement behavior

The scene model stages mutations and makes them committed only at a valid
`FrameCommit`. It returns `Accepted`, `Dropped`, or a specific rejection result,
along with the last presented generation and computed output damage. Output
shape changes damage the full output; surface visibility, geometry, stacking,
clipping, and opacity changes damage old and new bounds.

The current process path only decodes `FrameCommit` and sends a correlated
`FrameAcknowledged` with `RejectedIncompleteMetadata` and generation zero.
Therefore no accepted frame, framebuffer mutation, process-generated dump, or
buffer release is currently claimed.

## Validation

After configuring a build directory, focused implemented checks are:

```sh
meson test -C build compositor-geometry compositor-scene software-renderer \
  headless-output gwcomp-options --print-errorlogs
```

Run the complete suite with:

```sh
meson test -C build --print-errorlogs
```

End-to-end producer, golden-frame, reconnect, and Milestone 4 VM acceptance
commands remain completion work and are intentionally not documented here as
passing tests.
