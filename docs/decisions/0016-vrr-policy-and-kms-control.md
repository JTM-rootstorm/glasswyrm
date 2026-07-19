# 0016: Split VRR Intent, Policy, and Display Authority

Status: Accepted for Milestone 14 implementation

## Context

Variable refresh rate joins facts owned by three different processes. X11
clients can express a preference, the window manager can decide whether a
window is a suitable candidate, and only the compositor can validate the
surface and change KMS state. Treating any one of those facts as the final
answer would either move display authority out of `gwcomp` or let the
compositor silently invent window-management policy.

The DRM properties are also insufficient proof by themselves. A connector's
`vrr_capable` property describes display capability, while the atomic CRTC
`VRR_ENABLED` property is a userspace suitability hint. Positive acceptance
therefore needs both effective property readback and measured page-flip
cadence.

## Decision

Glasswyrm keeps the existing three-process transaction boundary:

- `glasswyrmd` owns the experimental `GW_VRR` preference, committed output
  policy, transaction coordination, and client notifications;
- `gwm` owns fullscreen, borderless-fullscreen, focus, membership, opt-out,
  and candidate selection;
- `gwcomp` owns surface validation, the final decision, effective state,
  timing, and KMS control.

GWIPC API 0.9 adds explicit capability, policy, candidate, compositor-state,
effective-state, and presentation-timing records. Historical peers negotiate
none of these records and preserve their prior wire shape. M14 retains wire
version 1.0 and SOVERSION 0.

Every output starts in policy `off`. The other modes are `fullscreen`,
`focused`, `app-requested`, and `always-eligible`. The first three candidate
modes require a GWM-selected window and a compositor-valid surface.
`always-eligible` is an explicit administrator override and requires no
window. A stable 64-bit reason mask explains every rejection and carries
informational `simulated-headless` or `manual-always-eligible` bits when
applicable.

The atomic DRM path discovers connector `vrr_capable` and CRTC `VRR_ENABLED`,
validates both off and on with TEST_ONLY commits, captures the original CRTC
property value, and initially modesets with VRR off. A transition is attached
to the same nonblocking atomic page flip as the framebuffer. Readback must
match before the effective state is published. VT release disables VRR before
dropping master; acquisition remodesets off and reevaluates only after the
session-active acknowledgement. Shutdown restores the original value and
verifies readback.

Headless simulation exercises the same decision and serialization paths but
reports `hardware_capable=false`, `simulated=true`, and synthetic timing. It
can never satisfy positive hardware acceptance.

## Consequences

- Application preference is advisory and never directly changes KMS.
- GWM policy remains deterministic and renderer-independent.
- `gwcomp` remains the sole final display authority.
- A failed policy, compositor, property, or readback stage rolls back without
  publishing client-visible success.
- VRR state never changes visible pixels or historical pixel hashes.
- Positive completion requires a reviewed physical target and kernel
  page-flip timestamps; fake DRM and QXL prove only negative and transaction
  behavior.
- Direct scanout, PRESENT pacing, DRI3, physical multi-output VRR, hotplug,
  HDR interaction, and vendor-specific APIs remain out of scope.
