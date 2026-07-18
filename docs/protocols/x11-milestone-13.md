# X11 Milestone 13 output and scaling profile

Milestone 13 is an opt-in output-model extension of the accepted Milestone 12
profile. It does not widen the general X11 or toolkit compatibility claim.
Without `--output-model`, setup bytes, one-output RANDR replies and events,
scene records, frames, and dumps retain their historical behavior.

## Dynamic screen and RANDR

Before the X11 listener opens, `glasswyrmd` obtains and validates compositor
inventory, multi-output GWM policy, and an initial compositor presentation.
New clients then receive the current logical root bounding box in core setup;
the root visual and resource identities remain fixed. Existing clients learn
accepted changes through selected RANDR events.

The advertised RANDR maximum remains 1.3. The M13 profile reports every stable
output, mode, enabled CRTC, logical position, current root extent, primary
output, and mapped transform. `SetCrtcConfig` succeeds only when it exactly
restates current state. Scale, layout, transform, and primary changes use the
same-UID `gwout` transaction path and never route a RANDR request into KMS.
Accepted changes emit one coherent ScreenChangeNotify, CRTC-change Notify, and
output-change Notify generation as applicable; rejected or rolled-back changes
emit none.

The pinned SDL 2.32.10 display probe is exercised against two headless outputs
as a repository M13 regression. That is not a claim of general multi-monitor
SDL support.

## `GW_SCALE` 0.1

The experimental `--scale-protocol` profile adds the bounded `GW_SCALE`
extension independently of `--game-compat`. It provides version and output
scale queries, per-window preferred scale and membership, integer client buffer
scale selection, retained depth-24 scaled-pixmap presentation, reset to legacy
backing, and selected `ScaleNotify` events. Requests and events are tested in
both client byte orders.

Legacy clients do not negotiate it and continue publishing scale-1 buffers,
which the compositor scales to each output. The only scale-aware client claim
is the repository raw-wire v0.1 probe. Existing Xlib, XCB, SDL, GTK, Qt,
Xft, and other toolkits are not claimed to negotiate `GW_SCALE`.

## Exact compatibility statement

Supported:

- several logical headless outputs
- one physical DRM output
- stable output inventory and capabilities
- integer and fractional compositor scaling
- all output transforms
- legacy client fallback scaling
- repository GW_SCALE v0.1 client
- multi-output RANDR reporting
- gwout/gwinfo control and diagnostics
- one workspace

Unsupported:

- several physical DRM connectors
- hotplug recovery
- physical mode setting through gwout/RANDR
- toolkit GW_SCALE integration
- Xft DPI integration
- output persistence
- VRR/HDR/color management

Additional deferred facilities include RANDR providers, monitors, leases,
gamma control, panning, clone configuration, direct scanout, DRI3, PRESENT,
GLX, DMA-BUF clients, and explicit GPU fences.
