# Milestone 10 Legacy KMS Path

The legacy path supports devices that can perform ordinary KMS modesets and
evented page flips but lack a complete usable atomic property set. It is a
presentation fallback, not a compatibility excuse for missing dumb buffers,
ambiguous output routing, scaling, or GPU rendering.

## APIs

Discovery and state capture use the same libdrm resource functions as the
atomic path: `drmModeGetResources`, `drmModeGetConnector`,
`drmModeGetEncoder`, `drmModeGetCrtc`, and, when available for discovery,
plane resource queries. Buffer allocation uses the same dumb-buffer ioctls,
`drmModeAddFB2`, and `DRM_FORMAT_XRGB8888`.

The initial presentation calls:

```text
drmModeSetCrtc(fd, crtc, framebuffer, 0, 0, &connector, 1, &exact_mode)
```

Subsequent presentations call:

```text
drmModePageFlip(fd, crtc, framebuffer, DRM_MODE_PAGE_FLIP_EVENT, cookie)
```

`drmHandleEvent` verifies the completion before the compositor transaction is
committed. Only one evented page flip may be in flight.

## Selection policy

`--drm-api legacy` forces this path. `--drm-api auto` selects it only when the
atomic attempt fails before the first real modeset and records why atomic was
unavailable. `--drm-api atomic` never falls back.

The selected connector and CRTC still follow the deterministic M10 eligibility
rules. A legacy driver may omit a usable universal primary-plane description;
plane ID zero in the diagnostic selection means the legacy CRTC path is the
scanout authority. Cloned routes remain unsupported.

## Saved state and restoration

Glasswyrm captures the selected connector's live CRTC route and the complete
CRTC framebuffer, position, activity, and exact mode timing before takeover.
Shutdown restores them with `drmModeSetCrtc`, reads them back, then removes the
Glasswyrm framebuffers and dumb buffers. Direct sessions subsequently drop DRM
master and restore terminal state.

Legacy restore can reconstruct only the state represented by the legacy KMS
API. That limitation is why automatic selection prefers a verified atomic
path when the driver exposes one.
