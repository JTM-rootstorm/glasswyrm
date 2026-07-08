# 0001: Meson Skeleton

Status: Accepted

## Context

The project specification recommends Meson + Ninja and lists early feature
options using names such as `backend_headless` and `backend_drm`.

Meson reserves option names beginning with `backend_`, and `werror` is already
a built-in Meson option.

## Decision

The initial skeleton uses:

- `headless_backend`
- `drm_backend`
- Meson's built-in `werror`

The rest of the early option surface follows the specification:

- `render_software`
- `render_gl`
- `render_vulkan`
- `asm`
- `asan`
- `ubsan`
- `tsan`
- `experimental`

## Consequences

The build remains idiomatic Meson while preserving the intent of the spec's
early configuration surface. Documentation and examples should use the actual
Meson option names.
