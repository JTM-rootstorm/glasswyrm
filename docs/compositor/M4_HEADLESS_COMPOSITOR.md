# Milestone 4 Headless Compositor

## Scope and status

Milestone 4 is the compositor-facing proof of Glasswyrm's three-process
architecture. It does not implement mapped X11 windows, WM policy, input,
DRM/KMS, scaling, HDR, VRR, or accelerated rendering.

Implemented today are bounded geometry and damage, staged scene state,
read-only sealed-memfd imports, a software renderer, a bounded headless
framebuffer, atomic PPM writing, CLI parsing, and a foreground `gwcomp` GWIPC
listener. The listener accepts one negotiated producer, dispatches the public
typed compositor contract, presents accepted frames atomically, and survives
producer disconnects.

## Command line

```text
gwcomp --ipc-socket PATH --dump-dir PATH [--once] [--max-frames N]
       [--help] [--version]
```

Both paths are required. The dump directory must not be a symbolic link.
`--max-frames` requires a positive integer. `--once` exits after one accepted
frame and `--max-frames` exits after the requested number of accepted frames.

## Topology and lifecycle

`gwcomp` listens on a local `AF_UNIX` `SOCK_SEQPACKET` endpoint as role
`Compositor` and accepts at most one role `TestProducer`. A self-pipe wakes its
poll loop for `SIGINT` and `SIGTERM`. A disconnected producer is discarded and
the compositor clears that producer's scene and mappings, while the listener
remains available for a new connection and complete replacement snapshot.

The required capability set is:

- descriptor passing and memfd buffers;
- snapshots;
- output and surface state;
- damage regions;
- SDR color metadata;
- frame acknowledgements.

The bootstrap is one `CompleteSession` snapshot containing the full
output, surface, and buffer state. Incremental mutations and commits before a
complete snapshot are rejected by the scene model.

## Supported contract subset

The M4 scene path is limited to output upsert/removal, surface upsert/removal,
surface damage, buffer attach/release, frame commit/acknowledgement, and
complete-session snapshot boundaries. Buffers are validated and mapped
read-only. Accepted commits render and dump an atomic framebuffer; rejected
commits leave the prior committed scene and framebuffer unchanged.

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

The process sends one correlated `FrameAcknowledged` for every frame commit.
An accepted acknowledgement reports the promoted generation after rendering
and durable frame-dump creation succeed. Incomplete or unsupported transactions
receive a specific rejection and do not change the visible framebuffer.

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

The process-level basic golden frame is covered by `gwcomp-golden`. The full
scenario fixture set, release/reconnect coverage, malformed-peer isolation,
sanitizer gate, and Gentoo VM acceptance are required before declaring the
milestone complete.
