# Milestone 13 output model

Milestone 13 adds an opt-in, component-neutral output model. `gwcomp` owns
backend inventory, `glasswyrmd` owns the committed protocol-visible layout,
`gwm` owns window assignment and placement, and `gwcomp` remains final render
and presentation authority. The profile is additive: without `--output-model`,
the historical one-output Milestone 12 setup, scene, frame, and dump contracts
remain unchanged.

## Model

An output descriptor records a stable 64-bit ID, bounded unique name, headless
or DRM kind, connection state, physical millimeters when known, supported
transforms, scale limits, capabilities, and bounded modes. An output state
selects a mode and records enabled and primary state, global logical position,
derived logical extent, rational scale, transform, and configuration
generation.

Output scale means physical pixels per logical desktop unit. It is a reduced
positive rational from `1/1` through `4/1`, with denominator at most 120. After
swapping physical width and height for quarter-turn transforms, logical extent
is derived exactly:

```text
logical_width  = ceil(transformed_width  * denominator / numerator)
logical_height = ceil(transformed_height * denominator / numerator)
```

All arithmetic is checked. A valid layout contains one through eight outputs,
at least one enabled output, exactly one enabled primary output, nonnegative
logical positions, and no overlap between enabled logical rectangles. Gaps are
allowed. Root size is the bounding box of all enabled logical rectangles and
is limited to 32767 by 32767 logical units.

The server derives root millimeters at a fixed 96 DPI. Root XID, colormap,
visual, depth, masks, and resource ranges never change when layout changes.

## Startup and ownership

An output-model session completes both GWIPC handshakes, queries and validates
the compositor inventory, constructs the dynamic screen, obtains a zero-window
multi-output policy result, and presents a zero-surface scene before opening
the optional control socket and X11 socket. An incomplete or empty inventory
fails before clients can connect.

Compositor reconnect requires the same descriptor IDs, names, modes, and
capabilities, then replays the committed layout and scene. A changed inventory
is a fatal divergence because hotplug is outside M13. GWM reconnect similarly
replays the complete multi-output policy snapshot and checks its deterministic
v3 policy hash.

See [output identities](M13_OUTPUT_IDENTITIES.md),
[layout transactions](M13_LAYOUT_TRANSACTIONS.md), and
[Decision 0015](../decisions/0015-output-model-and-scaling.md).

## Limits

- Output count: 8.
- Output name: 63 bytes.
- Physical extent per output: 4096 pixels per axis.
- Physical pixels per output: 16,777,216.
- Total output pixels: 67,108,864.
- Modes per output: 128.

This model does not add hotplug recovery, persistence, physical multi-connector
routing, or dynamic physical mode setting.

Milestone 14 preserves these descriptor, layout, and authority contracts while
adding opt-in VRR capability and policy records. VRR does not make output
layout mutable or expand the one-physical-connector boundary; see
[M14 VRR capabilities](M14_VRR_CAPABILITIES.md) and
[M14 KMS control](M14_VRR_KMS_CONTROL.md).
