# Milestone 12 EGL/GLES Renderer

Milestone 12 adds an opt-in OpenGL ES compositor while retaining the scalar
renderer as the default and exact reference. Both renderers return the same
component-neutral `SoftwareFrame`; EGL objects and textures never cross the
presentation boundary.

## Build and profile

Configure with `-Drender_gl=true`. This requires `gwcomp`, EGL 1.5 platform
entry points, and OpenGL ES 2.0. Meson links a probe for texture sub-image
uploads, scissoring, and readback. GBM is optional and is compiled in only when
the dependency is found. A build with `render_gl=false` does not discover or
link EGL, GLES, or GBM.

The renderer uses embedded, reviewed GLES 2.0 shaders, nearest sampling, and
premultiplied source-over blending. It supports XRGB8888, premultiplied
ARGB8888, per-surface opacity, rectangular clipping, stacking, cursor surfaces,
normal transforms, and scale 1/1. Other transforms or scales are rejected
during frame preflight.

## Context selection

When a primary DRM node or externally supplied DRM descriptor is explicit,
`gwcomp` resolves its associated render node through sysfs and accepts it only
as a real character device. A headless session may use a render node only when
exactly one validated candidate exists. Automatic DRM selection does not guess
an association before the DRM presenter has selected a device.

Context creation tries:

1. EGL on the validated GBM render node when GBM is available;
2. `EGL_MESA_platform_surfaceless`;
3. a 1x1 pbuffer when the selected display cannot make a no-surface context.

The renderer report records the successful EGL platform, EGL and GL identity,
GBM backend and render node when used, and bounded reasons for skipped or failed
earlier paths. Software classification is deliberately narrow: only llvmpipe
and softpipe renderer identities are classified as software.

Forced `--renderer gles` fails startup when no EGL path initializes and rejects
unsupported frames. `--renderer auto` selects software when context creation
fails. It may also retry an unsupported frame in software, but only when GLES
preflight rejected it before any GL state or output was changed. Fatal GL errors
and invalid producer buffers never trigger that per-frame fallback.

## Texture and damage behavior

Textures are cached by buffer ID and retired when the corresponding mapping is
no longer present. The named cache limit is 512 MiB, uses overflow-safe
accounting, and is injectable at a lower value for tests. A new texture receives
one tightly packed upload after compositor synchronization. Existing textures
receive `glTexSubImage2D` updates for the source rectangles intersecting output
damage; a bounded row-copy scratch path honors producer stride.

Each frame initializes an offscreen RGBA target from the prior
`SoftwareFrame` when its identity and dimensions match. Damage rectangles are
scissored, cleared, and redrawn bottom-to-top. Only damaged rectangles are read
back, with pixels outside damage retained from the previous frame. Frame report
records include upload count and bytes, live texture-cache bytes, readback
bytes, the renderer used for that frame, and any bounded auto-fallback reason.

## Equivalence boundary

Host tests require byte-exact software/GLES output for opaque XRGB scenes and a
maximum difference of one unit per RGB channel for premultiplied ARGB with
opacity. They also exercise clipping, a cursor surface, persistent texture
reuse, partial upload/readback metrics, the injectable cache limit, and forced
versus auto fallback policy. The scalar output and historical M4-M11 fixtures
remain authoritative.

Primary API references:

- [EGL 1.5 specification](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf)
- [EGL MESA platform surfaceless](https://registry.khronos.org/EGL/extensions/MESA/EGL_MESA_platform_surfaceless.txt)
- [OpenGL ES 2.0 reference pages](https://registry.khronos.org/OpenGL-Refpages/es2.0/)
