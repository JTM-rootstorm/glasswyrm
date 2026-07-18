# Milestone 12 Renderer Abstraction

Milestone 12 separates scene rendering from frame presentation without changing
the canonical output format. `gwcomp` still presents one component-neutral
`SoftwareFrame` to either the headless or DRM backend. Renderer selection does
not select, configure, or otherwise influence the presentation backend.

## Transaction boundary

`PresentationTransaction` validates the staged scene, attachments, and damage,
then submits a `RenderFrameRequest` to the selected `SceneRenderer`. The request
contains read-only scene state, stacking order, buffer mappings, attachments,
damage, and the last committed frame. A successful renderer returns a new
`SoftwareFrame`; only that frame is passed to the presenter and retained for a
pending presentation.

Renderer rejection occurs before presenter submission. A fatal renderer error,
including loss of required report evidence, is a fatal frame transaction.
Disconnect notification lets stateful renderers discard producer-owned caches
without affecting presenter state.

## Software reference

`SoftwareSceneRenderer` contains the composition loop previously embedded in
`PresentationTransaction`. It remains the default and has no EGL dependency.
The scalar clear, XRGB composition, premultiplied-ARGB validation, opacity
rounding, clipping, stacking, and cursor behavior are unchanged.

The historical damage behavior is also unchanged: each damaged rectangle clears
its destination area and causes every intersecting surface to be composited
through the existing scalar primitive. Pixels outside the damaged rectangle may
therefore be refreshed when they belong to an intersecting surface. Existing
M4-M11 frame fixtures remain the canonical exact-pixel evidence.

## Selection

`gwcomp` accepts:

```text
--renderer software|gles|auto
--renderer-report PATH
```

The default is `software`. A build without the optional EGL/GLES implementation
rejects forced `gles` during startup. In that build, `auto` selects software and
records the unavailable GLES path as a fallback reason. With GLES enabled,
`auto` may fall back for an unsupported frame only when GLES preflight rejects
the frame before changing GL state. It does not pretend that software
composition is hardware acceleration.

Presentation selection remains independent. For example,
`--backend drm --renderer software` uses the reference renderer followed by the
DRM presenter, while `--backend headless --renderer software` sends the same
canonical frame to the headless presenter.

## Renderer report

The optional report is a deterministic JSON-lines file. The target must not
exist, must have a real directory parent, and is created with exclusive,
no-follow, close-on-exec semantics. Its device and inode are revalidated before
every append. Replacement of the report path is fatal rather than silently
redirecting evidence.

The historical and output-model renderer adapters in one `gwcomp` process
share that secured descriptor. This preserves one exclusive-create boundary
while allowing the report to retain both legacy `frame` and output-model
`output-frame` records during profile transitions.

The first record identifies the requested and selected renderer, EGL/GLES/GL
fields, GBM/render-node fields, software-renderer classification, and bounded
fallback reasons. Unavailable graphics fields are JSON `null`, not invented
values. Each rendered frame records commit, generation, ordinal, disposition,
damage rectangle count, texture upload count and bytes, live texture-cache
bytes, readback bytes, an optional bounded per-frame fallback reason, and an
optional error. Software frames report zero texture uploads and readback bytes.

The [EGL/GLES profile](M12_EGL_GLES.md) extends the same interface, selection
factory, and report schema; it does not alter the presenter API or the canonical
`SoftwareFrame`.
