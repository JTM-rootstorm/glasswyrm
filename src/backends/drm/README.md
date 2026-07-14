# DRM/KMS Software-Scanout Backend

This internal backend presents `gwcomp`'s canonical software frame through one
Linux DRM/KMS primary node. It owns discovery, deterministic connector/mode/
CRTC/primary-plane selection, XRGB8888 dumb buffers, atomic or legacy KMS,
page-flip completion, reports, and ordered restoration.

The code in this directory must not become a renderer or learn X11/GWIPC scene
semantics. The component-neutral presentation interface lives under
`src/backends/output/`; VT and inherited-session ownership lives under
`src/backends/session/`. Real libdrm calls stay in `real_drm_api.cpp` and
`real_kms_api.cpp`; deterministic tests use the fake APIs.

Build explicitly with `-Ddrm_backend=true`. Headless remains the default and
has no libdrm dependency. See [`docs/output/`](../../../docs/output/) for the
operator-facing CLI, capability/property requirements, session boundary, VT
lifecycle, diagnostics, and hardware validation status.
