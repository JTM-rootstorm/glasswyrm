# Milestone 6 Metadata-Only Surfaces

`gwcomp` accepts a `ProtocolServer` peer in addition to the M4
`TestProducer`. ProtocolServer snapshots describe policy-approved top-level X11
windows without pretending that client pixels exist.

Each surface must set exactly `GWIPC_SURFACE_PRESENTATION_METADATA_ONLY`, carry
a nonzero X11 window ID, and have one matching `SurfacePolicyUpsert` with the
same surface and X11 identities. Duplicate or missing policies, unknown
surfaces, invalid geometry, and attached buffers reject the commit atomically.

An accepted metadata commit updates the staged scene, computes a deterministic
scene hash, and optionally appends one compact JSON line to `--scene-manifest`.
Visible ordering is bottom-to-top stacking order; hidden entries are ordered by
X11 ID. Manifest append uses a regular-file, no-symlink, locked, synchronized
path and rolls back a failed partial append.

Metadata commits disable raster output. They do not invoke the software
renderer, create frame-dump metadata, or produce a PPM. This is intentionally
not a fake one-pixel buffer path. M4 TestProducer snapshots continue to require
validated buffers and retain their golden raster behavior.

The current tests cover accepted multi-surface scenes, deterministic ordering
and hash, missing and duplicate policy, invalid buffer attachment, manifest
failure atomicity, role capabilities, a real process acknowledgement, and the
absence of raster dumps.

