# Milestone 7 Content Publication

Content publication bridges canonical window pixels in `glasswyrmd` to final
composition in `gwcomp`. It uses the existing GWIPC 1.0 buffer, damage, frame,
acknowledgement, release, and snapshot messages.

## Opt-in mode and peer profiles

`--software-content` is valid only when `glasswyrmd` is connected to both `gwm`
and `gwcomp`. Without the flag, integrated mode retains the M6 metadata-only
surface profile and produces no PPM. With it, the ProtocolServer requires:

```text
Snapshots OutputState SurfaceState SdrColorMetadata
FrameAcknowledgement WindowLifecycle
FdPassing MemfdBuffers DamageRegions
```

The last three capabilities are an indivisible set. `gwcomp` distinguishes the
unchanged M4 TestProducer, M6 metadata ProtocolServer, and M7 buffered
ProtocolServer profiles. The compositor profile's validation rules are defined
in [M7 buffered ProtocolServer surfaces](../compositor/M7_BUFFERED_PROTOCOL_SURFACES.md).

## Canonical and published storage

A visible top-level window has one mutable canonical XRGB8888 store and one
persistent memfd mirror. The server maps the memfd writable; the compositor
maps it read-only. The descriptor is close-on-exec, dimensions and allocation
are checked, and shrink and grow are sealed. The mapping remains writable by
the server, so it is not write-sealed.

`glasswyrmd` assigns a nonzero monotonic buffer ID. It copies dirty rows from
canonical storage, preserves the opaque X byte, and synchronizes the complete
mapping before frame submission. It never writes the mirror while a compositor
frame is in flight.

## Damage and frame arbitration

Each published window tracks `pending_dirty` separately from
`inflight_dirty`. Client drawing always mutates canonical pixels immediately.
When no frame is active, pending visible damage is copied into the mirror and
submitted as `SurfaceDamage` followed by an acknowledgement-required
`FrameCommit`. New drawing during that frame accumulates independently for the
next eligible commit.

Damage is clipped, empty rectangles are discarded, and rectangles are sorted
and merged deterministically. A region above 1,024 rectangles becomes one full
window region. Hidden-window drawing remains canonical and pending without
compositor traffic; the next visible lifecycle commit publishes its complete
current pixels.

There is one compositor transaction at a time, with strict priority:

1. reconnect, bootstrap, or full replay;
2. lifecycle submission or rollback;
3. incremental content damage.

Lifecycle commits fold pending content into their buffer update. Policy and
compositor serial sources are separate, and zero is never used.

## Attachment lifetime

An M7 complete snapshot seeds attachments from the last committed state.
Unchanged included surfaces retain their current attachment without another
`BufferAttach`. New and resized surfaces attach a matching buffer; omitted
surfaces retire theirs. Reconnect starts with no peer attachment knowledge, so
full replay includes every current buffer.

Resize allocates a replacement canonical store and published buffer after WM
policy dimensions are known. Acceptance atomically promotes the replacement;
the old buffer remains mapped until `BufferRelease(Replaced)`. Surface removal
similarly waits for `BufferRelease(SurfaceRemoved)`. Acknowledgement precedes
queued releases, but the server accepts valid releases before later traffic.
Unknown, duplicate, current-without-retirement, or reason-mismatched releases
are fatal contract errors.

## Rejection and restart

Canonical pixels remain protocol truth after presentation failure. On one
semantic rejection, the server marks all top-level windows fully dirty and
submits a complete buffered replay. A second rejection stops the server rather
than allowing divergent presentation; an already completed void X11 draw does
not receive a retrospective error.

Compositor disconnect merges in-flight damage back into pending damage and
drops peer attachment knowledge without disconnecting X11 clients. Reconnect
copies complete canonical content into the mirrors, reattaches all current
buffers, and requires an accepted full replay. The same scene and pixels yield
the same deterministic frame hash.

The compositor remains authoritative for software scene composition, PPM
encoding, frame manifests, and output hashes. `glasswyrmd` does not contain a
second desktop compositor.
