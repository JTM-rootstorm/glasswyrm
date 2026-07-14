# Milestone 10 Atomic KMS Path

Atomic KMS is preferred because it validates and changes the connector, CRTC,
mode, and primary plane as one request. It is not assumed merely because a
driver accepts `DRM_CLIENT_CAP_ATOMIC`.

## Capability and property requirements

The device must accept:

- `drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)`;
- `drmSetClientCap(DRM_CLIENT_CAP_ATOMIC, 1)`; and
- primary-plane enumeration with `DRM_FORMAT_XRGB8888` support for the selected
  CRTC.

`drmModeObjectGetProperties` and `drmModeGetProperty` must expose exactly one
usable binding for every required property:

| Object | Required properties |
|---|---|
| Connector | `CRTC_ID` |
| CRTC | `MODE_ID`, `ACTIVE` |
| Primary plane | `FB_ID`, `CRTC_ID`, `SRC_X`, `SRC_Y`, `SRC_W`, `SRC_H`, `CRTC_X`, `CRTC_Y`, `CRTC_W`, `CRTC_H` |

Missing, duplicate, zero-ID, too-narrow, or out-of-range bindings make the
atomic path unusable.

## Initial modeset

The exact discovered `drmModeModeInfo` timing is converted without synthesizing
a new mode. `drmModeCreatePropertyBlob` creates `MODE_ID`. The initial request
sets:

- connector `CRTC_ID` to the selected CRTC;
- CRTC `MODE_ID` and `ACTIVE=1`;
- primary-plane `FB_ID` and `CRTC_ID`;
- `SRC_X=SRC_Y=0` and source dimensions in 16.16 fixed point; and
- `CRTC_X=CRTC_Y=0` and exact destination dimensions.

Before taking over the display, the same request is submitted through
`drmModeAtomicCommit` with
`DRM_MODE_ATOMIC_TEST_ONLY|DRM_MODE_ATOMIC_ALLOW_MODESET`. The real initial
modeset uses `DRM_MODE_ATOMIC_ALLOW_MODESET` and completes synchronously.

## Page flips

After the first modeset, the selected primary plane's `FB_ID` is the only
changed property. `drmModeAtomicCommit` uses
`DRM_MODE_ATOMIC_NONBLOCK|DRM_MODE_PAGE_FLIP_EVENT` with a unique live cookie.
The presentation remains pending until `drmHandleEvent` returns the matching
CRTC, token, and nonzero sequence.

## Auto selection and fallback

`--drm-api atomic` requires the complete path above and fails initialization on
any capability, property, state-capture, mode-blob, or TEST_ONLY failure.

`--drm-api auto` tries the same path. It may fall back to legacy only before the
first real modeset, and records a bounded diagnostic reason. It does not fall
back after a submitted atomic modeset or page flip fails. `--drm-api legacy`
does not attempt atomic presentation.

## Restoration

Before takeover, Glasswyrm captures connector routing, exact CRTC state,
primary-plane geometry and framebuffer, and the required property bindings.
Shutdown creates a blob for the saved mode when active, commits the saved
property values with `DRM_MODE_ATOMIC_ALLOW_MODESET`, and reads connector,
CRTC, and plane state back for exact comparison before deleting Glasswyrm
framebuffers.
