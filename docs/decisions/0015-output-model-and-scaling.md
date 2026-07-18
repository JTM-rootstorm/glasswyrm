# 0015: Make Output Layout and Scaling Explicit

Status: Accepted for Milestone 13 implementation

## Context

Milestone 12 deliberately presents one fixed logical output. Its X11 screen,
window-manager work area, compositor scene, software frame, headless dump, and
DRM scanout all share the same dimensions. That identity is useful historical
evidence, but it cannot describe multiple outputs, fractional output scale,
transforms, or a client buffer whose pixels differ from its logical window
size.

Glasswyrm must add those concepts without moving authority between processes or
silently changing the established one-output profile.

## Decision

`gwcomp` owns output inventory and applied display state because it owns the
headless and DRM presentation backends. Outputs and modes have stable 64-bit
identities derived from persistent backend metadata. An output-model session
fails when the backend cannot provide stable identity; M13 does not implement
hotplug or accept identity drift across compositor restart.

`glasswyrmd` queries and validates compositor inventory before opening its X11
listener. It owns the committed logical layout, X11 root geometry, RANDR
objects, client-visible scale state, input bounds, and atomic output-control
transaction. `gwm` receives the complete output/work-area map and remains the
only owner of window assignment, placement, maximize, fullscreen, focus, and
stacking policy. `gwcomp` validates complete scene memberships, renders every
enabled output, and remains final presentation authority.

Physical mode pixels and global logical desktop coordinates are separate.
Each enabled output records physical extent, rational scale from 1/1 through
4/1, one of eight transforms, and a derived logical rectangle. Mapping applies
logical placement, rational scale, then output transform using checked integer
arithmetic. Enabled output rectangles may have gaps but may not overlap; the
X11 root begins at zero and extends to the maximum enabled output edge.

GWIPC API 0.8 adds bounded output inventory, policy-output, membership, scale,
and output-control records while retaining wire version 1.0 and SOVERSION 0.
The three M13 compositor capabilities and the two M13 GWM capabilities are
negotiated as complete profiles. Historical peers receive no new records and
retain their exact v1/v2 policy hashes, setup bytes, singleton scene, frame,
and dump formats.

The canonical software path renders a frame set keyed by stable output ID. A
surface carries one assigned primary output and the complete deterministic set
of outputs its global logical geometry intersects. Client buffer scale is an
independent integer from 1 through 4; output scale remains rational. Exact 1x
sampling stays unchanged, while fractional and downscaled paths use the fixed
filtering contract. GLES must match the software reference within documented
bounds and cannot mix renderers inside one frame-set transaction.

Renderer diagnostics retain the historical `selection` and `frame` JSONL
records unchanged. Output-model frames add a versioned `output-frame` record
with deterministic output-ID order. Each output reports physical damage,
texture upload and cache work, readback bytes, active sampling filters,
rational scale, transform, bounded fallback reason, and the maximum
software-reference channel error observed by fractional GLES sampling.

`GW_SCALE` 0.1 is an explicit experimental client contract for preferred
scale, membership notifications, and retained scaled-pixmap presentation. It
does not imply toolkit support. RANDR remains the standard output-reporting and
configuration surface for the bounded subset Glasswyrm implements.

## Consequences

- Historical one-output M12 behavior remains an independently tested profile.
- Output inventory must complete before GWM policy, compositor scene, control,
  or X11 readiness can be reported.
- Layout changes are all-or-nothing across GWM, compositor presentation,
  server state, RANDR/EWMH/input updates, and client events, with deterministic
  rollback on failure.
- Headless mode supports one through eight outputs; M13 DRM remains exactly one
  physical connector and rejects unsupported layout changes before KMS work.
- Physical multi-connector scanout, hotplug, toolkit scaling, direct scanout,
  VRR, HDR, and color management remain deferred.
