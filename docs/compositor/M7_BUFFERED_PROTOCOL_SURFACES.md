# Milestone 7 Buffered Protocol-Server Surfaces

Milestone 7 adds a buffered `ProtocolServer` application profile to the existing
GWIPC wire 1.0 compositor contract. It does not add a message or change the
public API version. `gwcomp` still owns final composition and frame output;
`glasswyrmd` publishes XRGB8888 window content but never composes the desktop.

## Peer profiles

`gwcomp` selects one internal profile from the peer role and negotiated
capabilities:

- `M4TestProducer` requires the original snapshot, output, surface, SDR,
  acknowledgement, FD, memfd, and damage capabilities. It retains the M4
  synthetic-surface raster behavior and rejects policy records.
- `M6MetadataProtocolServer` additionally requires window lifecycle, permits no
  buffer capabilities, requires metadata-only surfaces with matching policy
  records, and produces a scene manifest but no PPM.
- `M7BufferedProtocolServer` requires window lifecycle plus FD passing, memfd
  buffers, and damage regions. It requires buffered surfaces with matching
  policy records and produces the normal deterministic PPM and `frames.jsonl`.

A ProtocolServer negotiating any one of FD passing, memfd buffers, or damage
regions must negotiate all three. Partial combinations are rejected before
contract traffic is processed.

## Complete snapshots and attachments

An M7 complete snapshot replaces output, surface, and policy state while seeding
its pending attachment table from the last accepted frame. Consequently, an
unchanged included surface retains its buffer without another `BufferAttach`.
Omitted surfaces lose their attachment, new surfaces require an attachment, and
a resized surface must replace its buffer because dimensions are validated at
commit. Attachments whose surfaces are omitted are pruned before validation.

M4 complete snapshots continue replacing attachment state. M6 snapshots remain
buffer-free. A reconnect starts with an empty compositor, so the producer must
replay every current M7 attachment.

## Damage, raster output, and release

M7 accepts `SurfaceDamage` after the initial complete snapshot. Damage is
clipped and translated by the scene model, then rendered by the existing
software compositor from read-only XRGB8888 memfd mappings. The existing frame
dumper remains authoritative for PPM encoding, frame hashes, and
`frames.jsonl`.

Buffers replaced by an accepted frame are released with `Replaced`; buffers on
omitted surfaces use `SurfaceRemoved`; otherwise retired unattached buffers use
`ConsumerDone`. The process sends `FrameAcknowledged` before queued
`BufferRelease` records. Rejected commits do not promote scene or attachment
state. Disconnect drops all mappings and attachment knowledge.

