# Milestone 12 RANDR Profile

Milestone 12 exposes a bounded RANDR 1.3 reporting profile for game-client
compatibility. It describes one fixed, server-owned screen topology. It is not
a general output-management interface and does not claim complete RANDR 1.3
compatibility.

## Stable topology

The server reports exactly these objects:

- one root screen;
- one connected output, named `Glasswyrm-1`;
- one active CRTC;
- one current and preferred mode, named `1024x768`; and
- one primary output, which is the only output.

The output, CRTC, and mode identifiers are stable server-owned identifiers.
They are allocated outside client resource-ID ranges and remain unchanged for
the lifetime of the server. Their pixel dimensions, physical dimensions, and
refresh data come from the shared screen/output model rather than being copied
into independent RANDR-only state.

The reported topology has a single screen, no clones, normal rotation, and the
output positioned at `(0, 0)`. The screen resource timestamp and configuration
timestamp are deterministic server timestamps and advance only when the shared
output configuration changes.

## Supported requests

The profile supports these requests:

- `QueryVersion`;
- `SelectInput`;
- `GetScreenInfo`;
- `GetScreenSizeRange`;
- `GetScreenResources`;
- `GetScreenResourcesCurrent`;
- `GetOutputInfo`;
- `ListOutputProperties`;
- `QueryOutputProperty`;
- `GetOutputProperty`;
- `GetCrtcInfo`;
- `GetCrtcGammaSize`;
- `GetOutputPrimary`; and
- the restricted `SetCrtcConfig` behavior described below.

`QueryVersion` negotiates no version newer than 1.3. `GetScreenInfo` reports the
current size and normal rotation. `GetScreenSizeRange` fixes both the minimum
and maximum to the current screen dimensions. `GetScreenResources` and
`GetScreenResourcesCurrent` report the same one-output, one-CRTC, one-mode
topology. `GetOutputInfo` reports the output as connected, assigned to the only
CRTC, with the current mode also marked preferred and no possible clones.
`GetCrtcInfo` reports the current mode at `(0, 0)` with normal rotation and the
only output attached. `GetOutputPrimary` returns the only output.

No output properties are defined by this profile. `ListOutputProperties`
therefore returns an empty list. `QueryOutputProperty` and `GetOutputProperty`
provide deterministic empty-property behavior for valid requests and do not
create or mutate property state. `GetCrtcGammaSize` reports a zero-sized gamma
table because gamma control is outside this profile. Normal RANDR object and
atom validation still applies, including extension-specific errors for invalid
output or CRTC identifiers.

## Restricted configuration request

`SetCrtcConfig` is a compatibility acknowledgement, not a mode-setting path.
It succeeds only when the request exactly restates the current configuration:

- the stable current CRTC;
- the stable current mode;
- origin `(0, 0)`;
- normal rotation; and
- the current one-output set.

An accepted request returns `Success` and the current configuration timestamp
without changing state. Any request to change the mode, position, rotation, or
output set returns `Failed`. RANDR requests never perform DRM/KMS operations or
otherwise change the physical display configuration in Milestone 12.

## Selection and events

`SelectInput` records each client's event mask per selected window. Selection
state is removed when the client disconnects or the selected window is
destroyed. The profile defines exact wire encoding, in both byte orders, for
`ScreenChangeNotify` and `Notify` events. Events are delivered only to clients
whose selected mask covers the corresponding change.

Ordinary window-management changes do not imply an output change. In
particular, entering or leaving EWMH fullscreen does not emit a RANDR event.

## Explicit deferrals

The following RANDR facilities are outside the Milestone 12 profile:

- creating or destroying modes;
- changing the active mode or screen size;
- multiple outputs, CRTCs, screens, or clone relationships;
- providers;
- monitor objects;
- leases;
- hotplug discovery or notification;
- gamma-table reads or changes beyond the zero-size report;
- panning;
- transforms;
- output-property creation or mutation; and
- DRM/KMS output reconfiguration.

Requests outside the documented subset receive a deterministic unsupported
response or protocol error. They must not silently mutate the shared screen
model.

## Proof boundary

The profile is considered proven only when the following tests pass:

- raw protocol tests for every supported request in both client byte orders;
- exact reply, error, `ScreenChangeNotify`, and `Notify` wire-encoding tests;
- validation tests for stable objects, wrong object types, invalid atoms, and
  extension-specific output/CRTC errors;
- `SetCrtcConfig` tests for the accepted idempotent request and every rejected
  topology-changing field;
- event-selection, mask-filtering, window-destruction, and client-disconnect
  cleanup tests;
- integration tests showing that fullscreen policy emits no RANDR event; and
- an XCB probe that enumerates the screen resources, output, CRTC, mode,
  primary output, empty property set, and restricted configuration behavior.

These tests establish only this fixed one-output reporting contract. They are
not evidence of general RANDR output-management compatibility.

The additive Milestone 13 output-model profile is documented separately in
[`M13_RANDR.md`](M13_RANDR.md). The profile described here remains the exact
historical behavior when output-model mode is disabled.
