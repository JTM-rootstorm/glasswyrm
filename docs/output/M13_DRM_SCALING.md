# Milestone 13 single-output DRM scaling

Milestone 13 keeps the DRM/KMS hardware contract fixed at one device, one
connector, one CRTC, one primary plane, and the mode selected when `gwcomp`
starts. Output scale and transform are compositor properties; they do not
request KMS mode, plane rotation, or connector-position changes.

## Inventory and configuration

The selected connector publishes one stable DRM output descriptor. The
descriptor includes every enumerated connector mode, marks the selected mode
current, records physical millimeters when available, advertises all eight
software transforms and the bounded rational scale range, and marks physical
mode selection fixed.

The one-output DRM configuration profile accepts scale and software transform
changes while preserving the output identity, enabled/primary state, selected
physical mode, refresh, and logical origin `(0,0)`. A physical mode change is
reported as an unsupported mode. A logical-position change is an invalid
layout. Disabling the only output is also invalid because the M13 session
contract requires one enabled display.

The acceptance profile is:

```text
physical mode: 1024x768
logical origin: 0,0
scale: 4/3
logical size: 768x576
transform: Rotate180
```

## Rendering and scanout

The software or GLES output renderer applies scale and transform before the
DRM presenter is called. The resulting XRGB8888 frame always has the selected
native KMS width and height. The presenter rejects a logical-size frame before
performing any KMS mutation.

The existing blocking first modeset and asynchronous page-flip paths remain
unchanged. A committed output-layout generation change forces a full dumb
buffer copy with the report reason `output-configuration-changed`. Canonical
renderer and visible scanout hashes must remain identical.

Fake-DRM tests cover inventory capabilities, fixed mode and origin rejection,
accepted fractional scale plus `Rotate180`, the native framebuffer boundary,
the named full-copy reason, and canonical/scanout hash parity. Exact canonical
versus libvirt screenshot equality remains an explicit QXL VM hardware test.

M13 does not add multiple physical connectors, physical mode changes, hotplug,
KMS plane rotation, VRR, HDR, or color management.
