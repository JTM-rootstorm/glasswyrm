# Milestone 12 damage-aware DRM scanout

Milestone 12 changes the DRM dumb-buffer presenter from unconditional
full-frame copies to bounded damage-history copies. This is an internal scanout
optimization; the canonical composed frame remains the source of truth and the
visible scanout hash must still match it exactly.

## Generations and completion

Each dumb buffer records whether its contents are valid and the last generation
whose modeset or page flip completed. The presenter retains normalized damage
for at most eight completed generations. A submitted frame does not advance a
buffer or the history ring until KMS completion and evidence publication both
succeed.

When generation `N` is copied into a buffer containing generation `K`, the
presenter unions completed damage from `K + 1` through `N`. Rectangles are
clipped and normalized through the compositor damage-region implementation.
Only those visible XRGB8888 pixels are copied; pitch padding remains zero.

## Full-copy fallbacks

A complete frame is copied when:

- a dumb buffer is used for the first time;
- its generation has fallen outside the eight-frame history;
- the current frame has no usable damage;
- a bounded copy does not reproduce the canonical software-frame hash;
- virtual-terminal ownership has just been reacquired.

Buffer creation or a mode-size change also starts with invalid contents and
therefore takes the first-use path. VT release clears history and invalidates
both buffers so that resume cannot reuse uncertain contents.

## Evidence

With a DRM report configured, every successful damage-aware presentation stages
a `damage-copy` JSON record beside its modeset or flip record. It contains:

- `full_frame_bytes` and `copied_bytes`;
- normalized `copy_rectangles`;
- `history_span`;
- `full_copy_reason` (`none`, `first-use`, `history-miss`,
  `damage-unavailable`, `canonical-mismatch`, or `vt-resume`);
- saturating cumulative byte totals and `cumulative_copy_ratio_ppm`.

The ratio uses overflow-safe intermediate arithmetic. Evidence is committed
only after the corresponding KMS operation completes, so failed presentations
cannot claim copy savings or advance cumulative counters.

## Validation boundary

Unit tests cover first use of both alternating buffers, accumulated small
damage, history eviction, damage-unavailable fallback, failed page flips,
resume invalidation, zeroed pitch padding, report serialization, and canonical
versus scanout hash parity. Real DRM validation remains part of the fixed
Milestone 12 VM scenario.
