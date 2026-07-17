# 0014: Bound the Efficient SDL Buffer Path

Status: Accepted for Milestone 12 implementation

## Context

Milestone 11 proves one pinned terminal through the scalar compositor and
double-buffered DRM dumb scanout. SDL's X11 software-framebuffer backend needs
extension discovery, shared-memory image transport, display reporting, and
fullscreen window-manager state. The compositor also needs an optional GPU
renderer without creating a second presentation authority or invalidating the
software frame used by existing headless and DRM evidence.

## Decision

Milestone 12 exposes a static, opt-in `--game-compat` extension profile. The
profile implements only the documented BIG-REQUESTS, MIT-SHM, XFIXES, DAMAGE,
RENDER, COMPOSITE, and RANDR subsets. Historical clients continue to see every
extension absent and receive the existing setup reply. `glasswyrmd` remains
the owner of X11 resources, drawable pixels, extension semantics, SysV shared
memory, and canonical drawable damage.

The supported external-client boundary is SDL 2.32.10 from the verified
official release archive. It is built with the X11 software path, MIT-SHM,
XFIXES, and XRandR enabled while XInput2, Xcursor, Shape, Wayland, KMSDRM,
client OpenGL/OpenGL ES, Vulkan, and unrelated device/audio backends are
disabled. The accepted programs are the repository probe and the exact
official `testdraw2` and `testsprite2` source files recorded in the M12 client
manifest. This does not imply compatibility with other SDL releases or games.

Buffer publication advances additively to GWIPC API 0.7. An eventfd readiness
token makes CPU-buffer ownership explicit for game-compatible producers while
the historical synchronization-none contract remains valid. The producer
signals only after copying normalized dirty rectangles and does not modify an
in-flight published buffer. `gwcomp` consumes one token before reading damage
and retains ordinary frame acknowledgement as the ownership-release point.

Rendering and presentation remain independent. A scene renderer always
produces the component-neutral `SoftwareFrame`; the scalar implementation is
the default and canonical reference. An opt-in EGL/GLES implementation may
compose into an offscreen target and read damaged rectangles back into that
same frame type. Forced GLES fails closed. Auto selection may try GBM EGL,
surfaceless EGL, then software, and must report the selected path honestly.

DRM continues to scan out linear dumb buffers. Each buffer records its last
completed generation, and a bounded completed-damage history determines the
minimum safe copy into the next back buffer. Unknown history, resize, or VT
resume falls back to a complete copy. Generation state advances only after a
successful modeset or matching page-flip completion.

The authority rule remains unchanged: `glasswyrmd` owns protocol truth, `gwm`
owns fullscreen, borderless, geometry, focus, and stacking policy, and
`gwcomp` owns final composition and presentation.

## Consequences

- M12 is one-output, one-workspace, software-X11 SDL compatibility only.
- The software frame remains the exact historical and DRM evidence source.
- EGL/GLES changes compositor implementation, not X11 or KMS authority.
- Damage is preserved from drawable mutation through publication, upload,
  composition, and alternating dumb-buffer copies.
- GLX, DRI2/DRI3, PRESENT, client DMA-BUF, client OpenGL/Vulkan, direct
  scanout, multiple outputs, scaling, VRR, HDR, and broad game compatibility
  remain deferred.
