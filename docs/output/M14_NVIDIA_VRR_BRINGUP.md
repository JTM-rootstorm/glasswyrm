# Milestone 14 NVIDIA VRR bring-up

This procedure separates host preparation from Glasswyrm source diagnosis.
The read-only doctor is L3 evidence. It must pass before the minimal L4 KMS
probe or any complete physical run is considered.

## Human-owned preparation

The operator owns boot configuration, initramfs regeneration, NVIDIA module
reloads, rebooting, package changes, monitor controls, cables, and display
manager state. Glasswyrm tooling observes those facts and fails closed; it does
not change them.

For NVIDIA DRM, load-time vblank notifications must be enabled with
`nvidia_drm.vblank=1`. The doctor also records `nvidia_drm.modeset`, optional
`nvidia_drm.fbdev`, the loaded driver version and module flavor, and optional
`nvidia_modeset.conceal_vrr_caps`. Positive testing requires observed
capability concealment to be disabled. If the concealment parameter is absent,
the doctor records `unavailable` rather than guessing.

## Read-only classifications

The doctor report contains a stable `failure_reasons` array derived from its
individual checks. Use the first failed reason to choose the next action:

- vblank, concealment, active-VT, getty, session permission, or competing
  master failures are host prerequisites and do not justify a source rebuild;
- connector, EDID, mode, or capability failures require display, driver, and
  reviewed-profile investigation;
- malformed or absent `VRR_ENABLED` remains a DRM capability failure;
- exact off/on atomic controllability belongs to the non-mutating preflight of
  the L4 probe, not to the L3 doctor.

The doctor never writes sysfs, module parameters, boot files, KMS state, VT
state, input devices, or package state.

## Progression

After L3 passes, run only the minimal L4 NVIDIA VRR truth probe. Advance to the
three-process cadence stage only when that probe reports
`accepted-probe-distinction`. Property readback without distinguishable raw
page-flip cadence is not positive M14 evidence.
