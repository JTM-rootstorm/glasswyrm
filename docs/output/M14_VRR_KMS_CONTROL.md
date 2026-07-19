# Milestone 14 atomic KMS VRR control

The physical M14 path remains the M10-M13 single-device, single-connector,
single-CRTC, primary-plane XRGB8888 pipeline. VRR is an optional property on
that existing atomic transaction, not a second presentation path.

## Discovery and validation

The property cache retains connector `vrr_capable` and CRTC `VRR_ENABLED` by
standard name. The selected pipeline is controllable only after both
`VRR_ENABLED=0` and `VRR_ENABLED=1` succeed in TEST_ONLY atomic requests.
Optional properties are never required from historical, incapable, or legacy
profiles.

Initialization saves the original CRTC value. A controllable first modeset
explicitly includes zero, completes the modeset, verifies readback, and then
uses an ordinary page flip for a requested initial-on transition. This avoids
claiming an effective state before a real flip completes.

## Presentation

When desired state changes, or a test requests explicit reaffirmation, the
presenter adds `VRR_ENABLED` to the same nonblocking atomic request as the
framebuffer and requests a page-flip event. A state-only transition still
flips the committed framebuffer and receives an ordinary completion event.
There is no separate uncorrelated property commit.

After completion the presenter queries CRTC properties and requires the
readback to equal the desired value. A mismatch attempts a safe disable and
saved-state restoration, reports a fatal presenter divergence, and never
promotes the proposed compositor scene. Malformed readback remains visible in
diagnostics instead of being normalized into a successful false value.

## VT and process lifetime

VT release follows this order:

1. quiesce the pending flip;
2. atomically disable VRR on the current framebuffer;
3. verify readback zero;
4. complete the coordinated Inactive protocol;
5. drop DRM master and release the VT.

Acquisition reacquires master, remodesets the committed frame with VRR off,
and remains session-inactive until the protocol server acknowledges Active.
Policy reevaluation then uses a normal compositor transaction. Producer
disconnect disables the negotiated presenter VRR contract before committed
inventory is cleared; failure is fatal rather than allowing hardware and
published truth to diverge.

Shutdown restores the captured original property before framebuffer and DRM
master release and verifies the restored value. KMS, KD, active VT, getty,
and device ownership restoration remain part of the existing session report.

## Failure boundaries

- Missing capability or property disables VRR without disabling ordinary KMS.
- TEST_ONLY failure makes non-Off policy unsupported.
- Atomic transition failure preserves the old scene and effective state.
- Readback mismatch is fatal after recovery is attempted.
- Failure to disable before VT release is fatal.
- Legacy KMS never claims controllability or mutates `VRR_ENABLED`.
