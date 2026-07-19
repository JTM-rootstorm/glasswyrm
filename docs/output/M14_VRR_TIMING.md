# Milestone 14 VRR timing

Positive VRR evidence uses kernel page-flip completion timestamps. Userspace
dequeue time, render completion, and property write time are not substitutes.

## Samples

The DRM callback retains the CRTC event sequence, seconds, microseconds, and a
checked nanosecond timestamp. Consecutive events on the same CRTC produce one
interval. Sequence wrap is accepted; timestamp regression, overflow, a
different CRTC, or unavailable monotonic timestamps invalidates the interval
without fabricating a sample.

Statistics use a bounded 512-interval ring and record sample count, target
interval, pass count, minimum, maximum, integer mean, median, p95 absolute
error, and pass percentage in basis points. Reports contain no wall-clock
timestamps.

## Frozen acceptance threshold

For a configured target interval, a sample is within threshold when its
absolute error is no greater than:

```text
max(250,000 ns, target_interval / 100)
```

The positive run requires effective property readback one, at least 120 valid
intervals, and at least 75 percent within threshold. The negative run uses the
same in-range target with effective readback zero and requires fewer than 25
percent within threshold. A target that coincides with a fixed-refresh divisor
is rejected by the hardware configuration because it cannot distinguish the
two states.

A below-minimum run may be retained as diagnostic evidence when the reviewed
VRR range is trustworthy, but it is not the primary pass condition.

## Simulation and diagnostics

Headless simulation emits deterministic synthetic timestamps and marks every
record simulated. It validates policy, ordering, codecs, tools, and statistics
but cannot confirm hardware behavior. DRM JSONL emits separate `capability`,
`decision`, `timing`, `summary`, and `restore` records so pixel hashes remain
unchanged.
