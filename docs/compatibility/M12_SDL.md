# SDL 2.32.10 compatibility profile

Status: accepted for the pinned profile through the clean Gentoo VM evidence gate.

Milestone 12 targets only the unmodified SDL 2.32.10 X11 software-renderer
build, environment, and commands frozen in
`tests/compat/m12/clients.toml`. The source audit and host-side probes do not
by themselves establish compatibility. The clean `milestone12-runtime-test`
accepted this profile with a passing summary, an empty `evidence_errors` list,
and a validated checksum-protected archive.

## Verified release identity

The pinned source is the official SDL release artifact:

- version and tag: `SDL 2.32.10`, `release-2.32.10`;
- release commit: `5d249570393f7a37e037abf22cd6012a4cc56a71`;
- archive: `SDL2-2.32.10.tar.gz`, 7,630,262 bytes;
- archive URL:
  `https://www.libsdl.org/release/SDL2-2.32.10.tar.gz`;
- archive SHA-256:
  `5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`;
- detached signature URL: the archive URL with `.sig` appended;
- detached-signature SHA-256:
  `9533de95863efb5f3fb47fd22adda9be8f1d438b928b1287cea433d4d2ef10ad`;
- official signing-key page: `https://www.libsdl.org/signing-keys.php`;
- signing-key fingerprint:
  `1528635D8053A57F77D1E08630A59377A7763BE6`.

`tests/compat/m12/acquire_sdl.sh` downloads the archive, signature, and key
page from SDL's official HTTPS site with bounded retries, checks both artifact
hashes, imports the published key into
an isolated GnuPG home, checks the exact fingerprint, and verifies the
detached signature from Sam Lantinga. `verify_manifest.py` independently
checks the frozen manifest and the source files used by the workloads.

See [the source audit](M12_SDL_AUDIT.md) for the SDL X11 call paths that define
this profile.

## Exact build profile

SDL is built as a shared library with CMake and Ninja. The profile enables
only the X11 software path needed by the tests:

```text
CMAKE_BUILD_TYPE=Release       SDL_SHARED=ON
SDL_STATIC=OFF                 SDL_TEST=ON
SDL_TESTS=OFF                  SDL_X11=ON
SDL_X11_SHARED=ON              SDL_X11_XFIXES=ON
SDL_X11_XRANDR=ON              SDL_DUMMYAUDIO=ON
```

The following video, graphics, input, integration, and audio paths are
explicitly disabled:

```text
SDL_WAYLAND=OFF                SDL_KMSDRM=OFF
SDL_OPENGL=OFF                 SDL_OPENGLES=OFF
SDL_VULKAN=OFF                 SDL_HIDAPI=OFF
SDL_X11_XCURSOR=OFF            SDL_X11_XDBE=OFF
SDL_X11_XINPUT=OFF             SDL_X11_XSCRNSAVER=OFF
SDL_X11_XSHAPE=OFF             SDL_LIBUDEV=OFF
SDL_JOYSTICK=OFF               SDL_HAPTIC=OFF
SDL_SENSOR=OFF                 SDL_DBUS=OFF
SDL_IBUS=OFF                   SDL_DUMMYVIDEO=OFF
SDL_OFFSCREEN=OFF              SDL_RPI=OFF
SDL_VIVANTE=OFF                SDL_ALSA=OFF
SDL_JACK=OFF                   SDL_PIPEWIRE=OFF
SDL_PULSEAUDIO=OFF             SDL_SNDIO=OFF
SDL_OSS=OFF                    SDL_ESD=OFF
SDL_ARTS=OFF                   SDL_NAS=OFF
SDL_FUSIONSOUND=OFF            SDL_LIBSAMPLERATE=OFF
SDL_DISKAUDIO=OFF
```

The manifest is authoritative for the exact `-D` arguments. This profile does
not cover a distribution SDL build with a different dynamic X11 table or
enabled backend set.

SDL 2.32.10's test CMake project requires the disabled static SDL library.
The harness therefore builds the unmodified `testdraw2.c` and `testsprite2.c`
sources directly against `SDL2_test` and the installed shared SDL library.
The frozen external-source flags retain `-Werror` with only
`-Wno-unused-parameter` for an intentional callback parameter in
`testsprite2.c`.

The runtime environment is fixed:

```sh
LC_ALL=C
LANG=C
XMODIFIERS=@im=none
SESSION_MANAGER=
XAUTHORITY=/dev/null
DISPLAY=:99
SDL_VIDEODRIVER=x11
SDL_RENDER_DRIVER=software
SDL_AUDIODRIVER=dummy
LD_PRELOAD=tests/libgw_m9_fixed_time.so
```

The VM resolves the preload entry to the software build's existing
`libgw_m9_fixed_time.so`. The test-only shim freezes realtime calls used for
upstream workload seeding while leaving monotonic timeout clocks unchanged.

## Required external programs

The official programs are compiled without source modifications.

`testdraw2` uses source SHA-256
`e2d67b758d974bd9e07a4dcbe0107fbcf9ed342382a24ed63024699c7459cd5f`
and this exact command:

```text
testdraw2 --video x11 --renderer software --windows 1 \
  --geometry 640x480 --position 64,64 --blend none 100
```

The harness bounds its continuous event loop. Acceptance requires successful
X11/software-renderer initialization and no X protocol error while its point,
line, and rectangle workload is active.

`testsprite2` uses source SHA-256
`34c32afde10a35f72b6ebe80ede177c26ff70f528c7f76949865072654101ea8`
and the official `test/icon.bmp` asset with SHA-256
`f7b5cca4aabd94ba4cbaff14bde09ff1424403185e0613d48c77cc450064531e`:

```text
testsprite2 --video x11 --renderer software --windows 1 \
  --geometry 640x480 --position 64,64 --blend none \
  --iterations 120 100 icon.bmp
```

The fixed iteration value is the program's deterministic fuzzer seed and
stops sprite movement after 120 iterations. The VM keeps an unmodified
`testsprite2` process alive through input, restart, VT, repaint, and screenshot
evidence before terminating it through the harness.

The repository `m12_sdl_probe` supplements these external clients but does
not replace them. It links against the pinned SDL build and uses public SDL
APIs to prove one display/current mode, a resizable window, dirty software
surface updates, clipboard round trip, a custom core-cursor fallback,
fullscreen-desktop entry and exit, restored geometry, borderless windowed
state, and a real close event. Its controlled VM mode remains alive and
repaints while the hardware recovery scenarios run.

## SHM and fallback profiles

The primary profile starts `glasswyrmd` in game-compat mode with MIT-SHM
enabled. The raw probes cover both X11 byte orders. The XCB probes and SDL
workload evidence together must show the frozen extension
registry, BIG-REQUESTS, MIT-SHM 1.1, XFIXES
2.0, DAMAGE 1.1, RENDER 0.11, COMPOSITE 0.4, and RANDR 1.3 subsets. SDL's
software framebuffer must attach a same-process SysV segment and use
`ShmPutImage`; the producer signals its eventfd before `gwcomp` reads damaged
pixels.

The exact official `testdraw2` and `testsprite2` workloads query
`BIG-REQUESTS`, the normalized `OTHER` class, RANDR, and MIT-SHM, in that
order. In SDL 2.32.10, the shared X11 symbol group containing XFIXES also
requires `XIBarrierReleasePointer`; disabling XInput2 makes that group
unavailable. The SDL API probe and official workloads consequently do not
query XFIXES. The normalized official-client selector freezes that exact query
set. XFIXES remains mandatory and is proven independently by the raw/XCB
query, version, selection, and region probes.

The fallback profile restarts the server with exactly:

```text
--game-compat --disable-extension MIT-SHM
```

MIT-SHM must then disappear from extension discovery and SDL must fall back
to ordinary image upload. The raw BIG-REQUESTS probe sends an actual extended
4 MiB `PutImage`. This remains mandatory if the selected Xlib splits SDL's
own image into smaller requests; the evidence never invents an SDL trace.

Both profiles require clipped canonical damage. The normalized reports must
prove damaged GLES texture uploads and damage-history DRM copies, including a
partial copy smaller than the full frame and the required full-copy fallbacks.

## Live evidence contract

The harness installs Mesa with `-llvm`, selecting the bounded `softpipe` path
instead of building LLVM in the fixed 30 GiB guest. Renderer evidence must
still identify the actual EGL/GLES implementation and classify it as software.

The clean VM run must prove all of the following before this profile can be
marked accepted:

- the repository SDL probe, `testdraw2`, and `testsprite2` complete their
  frozen checks without protocol errors;
- after each fullscreen-ready handshake, real relative input clamps the
  pointer to the output origin and moves it to `(64,64)`; the harness waits
  for a new, content-stable committed frame before requesting the DRM
  screenshot, avoiding client-warp and capture-timing ambiguity;
- fullscreen desktop uses the one 1024x768 output, then exits to the exact
  saved windowed geometry;
- Motif borderless state remains windowed and preserves geometry;
- real libinput-path input reaches the live SDL client, clipboard and custom
  cursor paths work, and the close chord produces the real SDL close event;
- after that close, real relative input clamps the pointer to the output origin
  and moves it to `(64,64)` before the stable final frame is captured, keeping
  the exact software/GLES comparison independent of earlier client warps;
- the SDL probe and official sprite workload remain alive through GWM and
  compositor stop/start, receive replayed policy/buffer/texture state, and
  repaint afterward;
- VT release suspends presentation, VT acquire performs the required full
  modeset/copy, and both clients survive;
- producer and consumer eventfds are observable during the live SHM profile,
  the missing-token test proves no early read, and eventfd/SysV SHM/process,
  socket, input-device, device-FD, and texture-cache state returns to the
  recorded baseline;
- KMS, KD, VT, getty, and logind states match their before-run evidence;
- canonical and DRM screenshots are byte-identical;
- forced software and forced GLES render the windowed SDL probe,
  fullscreen-desktop, cursor-on-opaque-window, and stable final sprite scenes
  byte-for-byte identically;
- the renderer report records the selected EGL platform, EGL/GLES/GL identity,
  GBM or surfaceless path, software-renderer classification, damage, upload,
  cache, and readback metrics honestly;
- the normalized renderer, DRM, synchronization, workload, journal, and state
  evidence passes and every member of the archive matches `SHA256SUMS`.

The fixed release gate is:

```sh
./tools/gw-vm reset --yes
./tools/gw-vm milestone11-runtime-test --yes
./tools/gw-vm reset --yes
./tools/gw-vm milestone12-runtime-test --yes
```

The accepted run reports `passed: true` with no evidence errors. This page
records only that exact compatibility target, not a claim that SDL
applications or games generally work on Glasswyrm.

## Exact support boundary

Supported:

- SDL 2.32.10 exact X11 software-renderer build
- official testdraw2 accepted profile
- official testsprite2 accepted bounded profile
- one output and one workspace
- MIT-SHM and documented fallback
- fullscreen desktop and borderless windowed state
- software renderer
- constrained EGL/GLES compositor renderer
- real libinput profile from M11

Unsupported:

- arbitrary SDL versions
- SDL OpenGL/Vulkan renderers
- XInput2
- Shape
- full RENDER/COMPOSITE/RANDR
- DRI3/PRESENT/GLX
- direct scanout
- arbitrary games
- multiple outputs

## Explicitly unsupported

The M12 profile does not support or claim:

- another SDL release, a distro-default SDL configuration, another SDL render
  driver, or arbitrary games;
- Wayland, KMSDRM direct video, client OpenGL/OpenGL ES/EGL contexts, GLX,
  Vulkan, DRI2, DRI3, PRESENT, client DMA-BUF import, explicit GPU fences, or
  zero-copy/direct scanout;
- XInput2, full XKB, Xcursor themes, XFIXES pointer barriers/cursor hiding, or
  hardware cursor planes;
- shared pixmaps in MIT-SHM or BIG-REQUESTS beyond the fixed server cap;
- RENDER glyphs, filters, transforms, gradients, trapezoids, or arbitrary
  masks;
- Composite overlay windows;
- dynamic RANDR configuration, output changes, providers, monitors, leases,
  hotplug, multiple outputs, or output scaling;
- GTK or Qt compatibility, multiple workspaces, persistent production session
  packaging, or complete Gentoo split packages;
- VRR, HDR, color management, or other modern-output policy beyond the
  existing single-output foundation.
