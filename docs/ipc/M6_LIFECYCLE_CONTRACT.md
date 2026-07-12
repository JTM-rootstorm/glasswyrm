# GWIPC API 0.4 Lifecycle Contract

API 0.4 is additive to wire 1.0 and retains SOVERSION 0. Existing API 0.1,
0.2, and 0.3 message encodings remain unchanged.

## Additions

`GWIPC_CAP_WINDOW_LIFECYCLE` gates:

- `PolicyLifecycleWindowUpsert` (`0x0203`), which extends a complete policy
  window record with geometry/stack serials, sibling, stack mode, and flags;
- `SurfacePolicyUpsert` (`0x0112`), which pairs compositor surface identity
  with X11 policy metadata;
- `GWIPC_SURFACE_PRESENTATION_METADATA_ONLY`, the only M6 presentation flag.

All structures begin with `struct_size`, end in reserved-zero storage, and use
the public contract payload ownership rules. Unknown enums, flags, reserved
fields, invalid identifiers, invalid serial combinations, and truncated or
trailing payloads are rejected. Lifecycle records carry no descriptors.

## Policy transaction

The producer sends `SnapshotBegin(WindowPolicy)`, one context, every lifecycle
window, `SnapshotEnd`, and `PolicyCommit(AckRequired)`. `gwm` returns a complete
`PolicyWindowState` snapshot followed by a correlated
`PolicyAcknowledged(Reply)`. The consumer validates IDs, generation, exact
window membership, geometry, stacking, focus, reply sequence, and the canonical
`glasswyrm-policy-v1` hash.

## Compositor transaction

The producer sends `SnapshotBegin(CompleteSession)`, output state, paired
metadata-only `SurfaceUpsert` and `SurfacePolicyUpsert` records, `SnapshotEnd`,
and `FrameCommit(AckRequired)`. `gwcomp` accepts no buffer for metadata-only
surfaces and returns a correlated `FrameAcknowledged`. A buffer release on this
path is contract divergence.

The ProtocolServer compositor capability set excludes FD passing, memfd
buffers, and damage. The separate TestProducer role retains the M4 capability
set and raster path.

