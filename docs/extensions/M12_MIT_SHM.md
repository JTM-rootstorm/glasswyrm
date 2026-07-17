# Milestone 12 MIT-SHM Subset

The `--game-compat` profile exposes a bounded MIT-SHM 1.1 path for the pinned
SDL 2.32.10 X11 software framebuffer. Supported requests are `QueryVersion`,
`Attach`, `Detach`, `PutImage`, and the bounded `GetImage` path used by the
frozen probes. Shared pixmaps and `CreatePixmap` are unsupported. The reported
pixmap format is `ZPixmap`.

Each attachment is a client-owned resource containing the SysV `shmid`, exact
segment size, mapping, read-only state, and peer-credential evidence. Attach
uses Unix-socket peer credentials and `shmctl(IPC_STAT)` to require the segment
owner or creator UID to match the peer UID. Removed, inaccessible, mismatched,
or duplicate segments are rejected. Mappings are released exactly once by
`Detach` or client cleanup.

`PutImage` accepts the SDL profile: depth 24 pixels stored as 32 bits per pixel,
LSB-first `ZPixmap`, checked source geometry and stride, and a supported
destination/GC pair. The image is capped at 64 MiB. Checked offset and size
arithmetic prevents reads outside the mapping. Accepted pixels enter the core
`PutImage` mutation path so clipping, raster rules, and canonical drawable
damage remain single-sourced.

When requested, MIT-SHM Completion is emitted only after the drawable mutation
is accepted. The event carries the drawable, segment resource, request
identifiers, offset, recipient sequence, and recipient byte order. Malformed
traffic returns the documented core or `BadShmSeg` error without affecting
other clients.

Tests use real same-UID SysV mappings, both read-only and writable attachments,
both client byte orders, completion encoding, clipping, arithmetic and access
failures, disconnect cleanup, and multi-client isolation.
