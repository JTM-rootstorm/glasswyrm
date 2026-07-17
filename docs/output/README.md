# Output Backends

`gwcomp` composes one canonical XRGB8888 software frame and sends it to an
internal presentation backend. Presentation does not change X11, GWM, scene,
or GWIPC authority.

- [M10 DRM backend](M10_DRM_BACKEND.md): CLI, selection, frame lifecycle,
  diagnostics, and limits.
- [Device and session ownership](M10_DEVICE_AND_SESSION.md): direct and
  inherited DRM FD boundaries.
- [Atomic KMS](M10_ATOMIC_KMS.md): required capabilities, properties, and
  commits.
- [Legacy KMS](M10_LEGACY_KMS.md): fallback modeset and page flips.
- [VT lifecycle](M10_VT_LIFECYCLE.md): release, acquire, restoration, and
  signal handling.
- [Hardware validation](M10_HARDWARE_VALIDATION.md): VM and spare-VT
  procedures, evidence, recovery, and current acceptance status.

The VT lifecycle also documents the capability-gated M11 coordination which
suspends server-owned libinput before display release and resumes it after
reacquisition.

Headless remains the default build and test path. Enabling DRM is explicit
with `-Ddrm_backend=true`; a default build neither discovers nor links libdrm.
