# Milestone 14 VRR timing

Positive VRR evidence uses kernel page-flip completion timestamps. Userspace
dequeue time, render completion, and property write time are not substitutes.

## Samples

The DRM callback retains the CRTC event sequence, seconds, microseconds, and a
checked nanosecond timestamp. Consecutive events on the same CRTC produce one
interval. Sequence wrap is accepted; timestamp regression, overflow, a
different CRTC, or unavailable monotonic timestamps invalidates the interval
without fabricating a sample.

When a driver reports event sequence zero, the real DRM adapter also attempts
`drmCrtcGetSequence` after consuming the matching event. The raw page-flip
sequence and timestamp remain unchanged and authoritative. The queried 64-bit
sequence and nanosecond timestamp are retained separately with
`CrtcSequenceQuery` source and an explicit correlation result.

If that modern query fails, the adapter may issue the standard legacy
`drmWaitVBlank` current-vblank query for the matching lease-relative CRTC
resource index. The request is relative zero without event or next-on-miss
flags, so it does not request a future vblank or change KMS object state.
The kernel may transiently acquire vblank interrupt accounting while answering
the query. The returned 32-bit counter is extended locally across wrap with a
half-range ambiguity check and is tagged separately as `LegacyVBlankQuery`;
it is not represented as a native kernel 64-bit sequence.

A query sample from either source is cadence-eligible only when the query
succeeded, monotonic timestamps are supported, both values are nonzero, the
queried timestamp falls within the page-flip event's one-microsecond timestamp
quantization window, and the queried sequence and timestamp advance. Query
failure, a later vblank, an ambiguous 32-bit change, or a regression does not
invalidate the completed flip.
VT release and every blocking modeset clear the per-CRTC correlation baseline,
including the locally extended legacy counter, so the first tightly correlated
sample after acquire or a sequence rebase starts a new timing period instead
of being compared with the prior scanout period.

The standard DRM `flip` record serializes the queried source, correlation,
sequence, timestamp, and cadence-eligibility verdict separately. M14
`vrr-timing` records continue to serialize the raw page-flip event fields, and
the physical acceptance calculation does not consume either diagnostic query
source.

The compositor and DRM presenter report raw presentation facts only:

- kernel page-flip timestamp and sequence;
- interval between valid consecutive timestamps;
- effective `VRR_ENABLED` readback state;
- transition serial; and
- the nominal interval derived from the selected output mode.

That nominal mode interval is a mode fact, not an application cadence target.
For example, a 144 Hz mode has a nominal interval near 6.94 ms while an
application may intentionally present at 70 Hz, near 14.29 ms. Runtime timing
records preserve both facts without claiming that the 70 Hz interval passed or
failed VRR acceptance.

Runtime summaries contain raw interval count, minimum, maximum, integer mean,
and median together with enabled and disabled period counts and a bounded count
of presentations whose kernel timestamp was unavailable. They do not contain a
cadence pass count, pass percentage, or absolute error against the nominal
mode. Reports contain no wall-clock timestamps.

## Hardware acceptance threshold

The physical hardware harness, not the compositor, owns the configured target
cadence and the M14 acceptance verdict. For that configured target interval, a
sample is within threshold when its absolute error is no greater than:

```text
max(250,000 ns, target_interval / 100)
```

The positive run requires effective property readback one, at least 120 valid
intervals, and at least 75 percent within threshold. The negative run uses the
same in-range target with effective readback zero and requires fewer than 25
percent within threshold. A target that coincides with a fixed-refresh divisor
is rejected by the hardware configuration because it cannot distinguish the
two states.

Successful `VRR_ENABLED` property readback proves that the requested KMS state
was effective for the flip. It does not by itself prove cadence behavior.

A below-minimum run may be retained as diagnostic evidence when the reviewed
VRR range is trustworthy, but it is not the primary pass condition.

## Simulation and diagnostics

Headless simulation emits deterministic synthetic timestamps and marks every
record simulated. It validates policy, ordering, codecs, tools, and raw timing
reporting but cannot confirm hardware behavior. Its JSONL record tags are
`capability`, `decision`, `timing`, `summary`, and `restore`. DRM JSONL uses
`vrr-capability`, `vrr-decision`, `vrr-timing`, `vrr-summary`, and
`vrr-restore`. The distinct vocabularies are frozen in the deterministic M14
fixtures and keep pixel hashes unchanged.

Reason masks are authoritative. Engine state, backend reports, and fixtures
use the canonical CamelCase names from the 33-bit registry; `gwinfo`/`gwout`
render those same ordered bits as kebab-case command-line names. Neither form
may invent, drop, or reorder a reason.
