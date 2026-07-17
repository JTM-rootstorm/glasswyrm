# Milestone 12 SDL 2.32.10 Source Audit

Status: implementation input; runtime acceptance is not yet claimed

## Pinned release

Milestone 12 uses the unmodified official SDL 2.32.10 release at commit
`5d249570393f7a37e037abf22cd6012a4cc56a71`. The official archive SHA-256 is
`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
Its detached signature verifies with the official Sam Lantinga signing key,
fingerprint `1528635D8053A57F77D1E08630A59377A7763BE6`. Exact URLs, hashes, build
switches, program source hashes, environment, and argv are frozen in
`tests/compat/m12/clients.toml`.

SDL 2.32.10's test-directory CMake logic links every program to
`SDL2-static`. Because the accepted profile deliberately sets
`SDL_STATIC=OFF`, the harness builds the two hash-verified, unmodified official
program sources directly against the upstream `SDL2_test` archive and the
installed shared SDL library.

## Audited X11 paths

`SDL_x11framebuffer.c` queries MIT-SHM, allocates a same-process SysV segment,
attaches it with `XShmAttach`, constructs an `XShmCreateImage`, updates dirty
rectangles with `XShmPutImage`, and detaches during framebuffer teardown. If
MIT-SHM is absent, the same backend creates a normal `XImage` and uses
`XPutImage`; sufficiently large requests require BIG-REQUESTS or Xlib-side
splitting.

`SDL_x11modes.c` queries XRandR and enumerates current screen resources,
outputs, CRTCs, modes, the primary output, output properties, and change-event
selection. The accepted Glasswyrm profile reports exactly one connected
output, one active CRTC, and one preferred/current mode. Mode-setting calls are
accepted only when they preserve that topology and mode.

`SDL_x11window.c` creates a TrueColor colormap for the selected visual and
passes it through `XCreateWindow`. Fullscreen desktop is requested with the
EWMH `_NET_WM_STATE_FULLSCREEN` ClientMessage. Borderless state is expressed
through `_MOTIF_WM_HINTS`. Window state reads also require deterministic root
and per-window EWMH properties.

SDL initialization also sends core `ForceScreenSaver(Reset)`. Glasswyrm
accepts the protocol's Reset and Active modes as side-effect-free requests
because it does not implement a server-owned screen saver; invalid modes and
request lengths retain the required core errors.

`SDL_x11mouse.c` first uses Xcursor when compiled in, then falls back to core
pixmap cursors and core font cursors. The accepted build disables Xcursor so
the existing M11 core cursor model remains authoritative. XFIXES pointer
barriers are not required because the accepted profile does not request mouse
confinement.

`SDL_x11clipboard.c` creates an unmapped core window and uses the ordinary
selection/property protocol. `SDL_x11xfixes.c` negotiates XFIXES and subscribes
the root window to owner-change notifications for CLIPBOARD and PRIMARY. The
accepted XFIXES version is 2.0; pointer-barrier symbols may exist in the SDL
dynamic table but are outside the frozen workload.

The dynamic X11 symbol table confirms the required core colormap/window,
MIT-SHM, XFIXES selection, and XRandR entry points. Optional XInput2, Xcursor,
Shape, XScreenSaver, XDBE, Wayland, KMSDRM, and client graphics APIs are
disabled at build time so their presence is not part of the compatibility
contract.

## Official workloads

The exact official `testdraw2.c` source hash is
`e2d67b758d974bd9e07a4dcbe0107fbcf9ed342382a24ed63024699c7459cd5f`.
It draws points, lines, and rectangles continuously. Its random seed comes
from `time(NULL)`, so the VM harness uses the repository fixed-time preload
and terminates it after a bounded observed-frame count.

The exact official `testsprite2.c` source hash is
`34c32afde10a35f72b6ebe80ede177c26ff70f528c7f76949865072654101ea8`.
With `--iterations 120`, the unmodified program uses 120 as the deterministic
fuzzer seed and stops sprite movement after exactly 120 iterations. The
accepted invocation uses blend mode `none`, 100 sprites, one 640x480 window,
the software SDL renderer, and the official `icon.bmp` asset.

## Compatibility boundary

This audit identifies implementation requirements; it does not by itself
establish runtime compatibility. Acceptance requires the raw and XCB extension
probes, repository SDL probe, official workloads, exact software/GLES frame
equivalence, DRM screenshot parity, live input/fullscreen/clipboard/close
checks, restart and VT recovery, and the clean M12 Gentoo VM evidence gate.
