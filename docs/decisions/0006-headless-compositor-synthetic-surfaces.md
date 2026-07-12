# 0006: Use a Transactional Headless Compositor

Status: Accepted; integration incomplete

## Context

The first compositor milestone must prove presentation behavior without
depending on X11 window mapping, window-manager policy, input, DRM/KMS, or an
accelerated graphics API. It also needs deterministic output suitable for
headless tests and a narrow failure boundary for malformed producers.

## Decision

Milestone 4 uses one `gwcomp` process accepting at most one same-UID GWIPC
`TestProducer`. The producer describes at most one enabled headless output and
a bounded collection of synthetic surfaces. A complete-session snapshot is
required before incremental mutations or a frame commit are eligible for
presentation.

Scene mutations are staged separately from committed presentation state. A
valid `FrameCommit` atomically replaces committed scene metadata; rejection
leaves the last committed framebuffer and scene visible. State changes damage
both old and new bounds, and explicit surface damage is clipped to the surface,
its optional clip rectangle, and the output. Equal stacking values are ordered
by surface identifier.

Buffers are producer-created memfds transferred through GWIPC descriptor
passing. The compositor owns its received descriptor and read-only mapping
until replacement, surface removal, or disconnect makes the buffer releasable.
The milestone deliberately uses no explicit GPU synchronization: a producer
must finish writing before committing and must not reuse storage before its
release notification.

Rendering uses the reference C++ software path. The only accepted buffer
formats are XRGB8888 and premultiplied ARGB8888. Accepted frames are intended to
produce binary PPM dumps and hashes over their RGB payload, giving tests a
simple, dependency-free golden artifact.

Parent/child surfaces, transforms, and scaling are rejected rather than
silently approximated. They require policy and coordinate semantics that this
single-output proof does not establish.

## Current implementation boundary

The scene, damage, read-only memfd mapping, software blend, bounded headless
output, and atomic PPM dump primitives exist and have focused tests. `gwcomp`
now owns the real listener and connection reactor, but currently acknowledges
every received frame commit as `RejectedIncompleteMetadata`. Contract dispatch
into the scene and buffer lifetime, rendering, release messages, accepted-frame
dumps, and the synthetic producer are not integrated yet.

## Consequences

- Compositor behavior can be tested without display hardware.
- A rejected transaction cannot partially update presentation state.
- The first renderer and artifacts are intentionally simple and deterministic.
- This decision does not connect `glasswyrmd` or `gwm` to `gwcomp` and does not
  establish real X11 client compatibility.
