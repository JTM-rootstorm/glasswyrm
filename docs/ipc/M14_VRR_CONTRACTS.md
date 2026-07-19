# Milestone 14 VRR transaction contracts

The fixed payload layouts, message IDs, capability bits, enum registries, and
validation rules are documented in [M14 VRR contract](M14_VRR_CONTRACT.md).
This document records how those records participate in live transactions.

## Capability profile

API 0.9 adds `VrrMetadata`, `VrrPolicy`, and `PresentationTiming` while keeping
wire 1.0 and SOVERSION 0. Historical peers negotiate none of the M14 records.
Every VRR payload has zero file descriptors and uses the established
`struct_size`, reserved-zero, bounded enum, exact boolean, and known reason-mask
rules.

## Inventory and policy

On startup and compositor reconnect, the server queries an Outputs snapshot.
The M14 response adds exact capability, policy, committed state, and available
timing records for the current output set. Inventory completes before the X11
listener or output-control socket is considered ready.

A GWM M14 policy snapshot contains one output VRR input per output and one
window VRR input per non-override-redirect policy candidate. Output and window
results must have exact cardinality, agree on the selected candidate, and
reproduce policy hash v4. The server creates a safe unmanaged SurfaceVrrState
for other nonmetadata top-level surfaces so the compositor scene still has one
VRR record per such surface.

## Compositor transaction

Each M14 complete scene contains one output policy record per output and one
surface VRR state per nonmetadata window surface. `gwcomp` validates IDs,
candidate correspondence, generations, booleans, enums, and reasons before
calling the decision engine or presenter.

The response batch is preflighted before hardware mutation. For each presented
output it contains effective state then presentation timing, followed by the
ordinary FrameAcknowledged and releases. The server validates the complete
batch before promotion. A rejection preserves previous GWM, server,
compositor, and effective state and emits no success event.

## Output control and lifecycle

`gwout --vrr` reuses the complete-layout output-control transaction even when
geometry is unchanged. Focus, map, unmap, fullscreen, borderless geometry,
preference, and destruction reuse the lifecycle transaction. In both cases
policy is staged first, compositor presentation second, and server-visible
state last.

Reconnect retains the in-flight stage. A compositor reconnect during a staged
VRR transaction bootstraps from that retained scene, not the old accepted
scene, and the accepted bootstrap completes the original transaction exactly
once. GWM or compositor divergence outside the frozen inventory/profile is
fatal because hotplug remains out of scope.
