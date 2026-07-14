# 0012: Present the Canonical Software Frame Through DRM/KMS

Status: Accepted and validated

## Context

The headless compositor already produces one deterministic XRGB8888 software
frame and completes a GWIPC frame transaction synchronously. Milestone 10 needs
to put that same frame on a Linux display without creating a second renderer,
moving protocol semantics into the compositor, or introducing a GPU stack.

DRM presentation is asynchronous after the initial modeset. Linux virtual
terminal ownership also makes display access revocable, so scene promotion,
producer acknowledgement, buffer release, and shutdown can no longer be tied
directly to the end of software composition.

## Decision

`gwcomp` keeps a component-neutral `SoftwareFrame` as the canonical rendering
result and passes a read-only view to an internal presentation backend.
`HeadlessPresenter` completes immediately and retains the existing PPM path.
`DrmPresenter` copies the complete visible frame into one of two linear
XRGB8888 dumb buffers. The first presentation performs a blocking modeset;
later presentations allow exactly one page flip in flight.

M10 selects exactly one connected, desktop connector, one compatible CRTC, and
one exact-size mode. Atomic KMS is preferred only when universal planes,
atomic capability, one usable primary plane, the complete required property
set, and a TEST_ONLY modeset all succeed. `--drm-api auto` may then fall back
to the legacy modeset/page-flip path. Forced atomic mode fails instead of
falling back.

A pending DRM frame owns the candidate scene, attachments, releases, mirror
evidence, and report evidence until the matching page-flip event is verified.
Only then does `gwcomp` promote the scene, release retired buffers, and send
`FrameAcknowledged`. A two-second missing or mismatched completion is fatal.

Direct sessions open a primary DRM node, acquire DRM master, and own a specific
`/dev/ttyN` in `VT_PROCESS`/`KD_GRAPHICS` mode. Inherited sessions duplicate a
caller-supplied DRM FD and never acquire or drop master or manipulate a VT;
the external session owner retains those responsibilities. The inherited seam
is intentionally suitable for a future logind or seatd integration without
linking either library in M10.

On normal shutdown and handled fatal paths, `gwcomp` first restores and reads
back the saved connector, CRTC, mode, framebuffer, and primary-plane state. It
then removes Glasswyrm framebuffers and dumb buffers, drops DRM master, restores
KD and VT modes, and returns to the previously active VT. VT release drops
master after quiescing presentation; acquire regains master and performs a full
modeset of the last committed frame.

This ordering is a best-effort process guarantee. `SIGKILL`, kernel failure,
device removal, and machine power loss cannot run userspace restoration.

GWIPC API 0.5.0, SOVERSION 0, and wire 1.0 remain unchanged. Presentation is a
private `gwcomp` boundary below the existing compositor contracts.

## Consequences

- Headless remains the default and has no libdrm dependency.
- DRM builds require Linux, libdrm KMS APIs, and software rendering.
- Scanout is a complete CPU copy; there is no acceleration or direct scanout.
- The M10 output is one unscaled, unrotated XRGB8888 output.
- Real input, hotplug recovery, multiple outputs, cursor/overlay planes, GBM,
  DMA-BUF scanout, explicit fences, DPMS, VRR, HDR, and color management remain
  deferred.
- Forced termination may require manual VT or KMS recovery.
- Mocked host coverage and the configured Gentoo QXL graphical-console route
  both validate the output, VT, restoration, and evidence boundaries.
