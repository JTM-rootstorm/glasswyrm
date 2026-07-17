# X11 Milestone 12 game-client profile

Status: implemented with host coverage; live Gentoo VM acceptance is pending.

Milestone 12 adds an opt-in X11 profile for the exact SDL 2.32.10 X11
software-renderer build. It is not enabled by default and is not a claim of
general SDL, game, toolkit, or extension compatibility. The external-client
claim remains pending until the clean M11-to-M12 VM sequence passes with a
complete evidence archive.

## Activation and setup

The profile is selected with `glasswyrmd --game-compat`. It requires an
experimental build, integrated `gwm`/`gwcomp` peers, and `--software-content`.
`--disable-extension NAME` may be repeated only in this profile and is used by
the acceptance harness to prove the MIT-SHM fallback.

Without `--game-compat`, setup replies, dynamic atom allocation, and the
historical all-absent `QueryExtension`/empty `ListExtensions` behavior remain
unchanged. With the profile active, setup adds these pixmap formats:

| Drawable depth | Bits per pixel | Scanline pad |
| ---: | ---: | ---: |
| 1 | 1 | 32 |
| 8 | 8 | 32 |
| 24 | 32 | 32 |
| 32 | 32 | 32 |

The root remains depth 24 with the existing TrueColor visual. Depths 1, 8,
and 32 add no visuals. Both client byte orders are supported.

## Extension registry

The immutable game-profile registry is:

| Extension | Major | First event | First error | Advertised version |
| --- | ---: | ---: | ---: | ---: |
| BIG-REQUESTS | 128 | 0 | 0 | 1.0 |
| MIT-SHM | 129 | 64 | 128 | 1.1 |
| XFIXES | 130 | 65 | 129 | 2.0 |
| DAMAGE | 131 | 66 | 130 | 1.1 |
| RENDER | 132 | 0 | 131 | 0.11 |
| Composite | 133 | 0 | 0 | 0.4 |
| RANDR | 134 | 67 | 136 | 1.3 |

`QueryExtension` matches exact case-sensitive names. `ListExtensions` returns
enabled names in ascending major-opcode order. Disabling one extension removes
only that entry; it does not renumber any other opcode, event, or error range.
Extension request, event, reply, and error encoding preserves the requesting
client's byte order and sequence.

## Implemented extension subsets

### BIG-REQUESTS 1.0

`Enable` is idempotent and enables the standard extended request header for
that client. The advertised and enforced maximum is 4,194,304 four-byte units
(16 MiB). Incremental framing preserves pipelined requests, accounts the full
request against the reactor budget, and rejects a zero ordinary length before
enablement or an extended length outside the bounded range.

### MIT-SHM 1.1

The profile implements `QueryVersion`, `Attach`, `Detach`, `PutImage`, and the
bounded probe `GetImage` path. `CreatePixmap` and shared pixmaps are absent.
SysV segments must be owned or created by the same UID as the Unix-socket peer;
offsets, geometry, stride, mapping size, access mode, and destination/GC
compatibility are checked before mutation. Accepted `ShmPutImage` pixels reuse
the core image path and canonical damage hook. Optional completion events are
sent after the mutation is accepted, and every mapping is detached exactly
once on request or client cleanup.

### XFIXES 2.0 and DAMAGE 1.1

XFIXES implements `QueryVersion`, `SelectSelectionInput`, and the bounded
rectangle-region requests `CreateRegion`, `DestroyRegion`, `SetRegion`,
`CopyRegion`, `UnionRegion`, `IntersectRegion`, `SubtractRegion`,
`TranslateRegion`, `RegionExtents`, and `FetchRegion`. Owner changes for
`PRIMARY` and `CLIPBOARD`, owner-window destruction, and owner-client cleanup
produce the documented selection notification. Regions are normalized,
bounded, client-owned resources; invalid identifiers use `BadRegion`.

DAMAGE implements `QueryVersion`, `Create`, `Destroy`, `Subtract`, and `Add`
for `NonEmpty` and `BoundingBox`. Core drawing, image upload, MIT-SHM upload,
RENDER operations, clear, copy, text, and child-composition changes feed the
same canonical mutation hook used by internal publication. Damage resources
accumulate normalized rectangles, support repair subtraction, emit exact
notifications, and disappear with their owner or watched drawable. Invalid
identifiers use `BadDamage`.

### RENDER 0.11 and COMPOSITE 0.4

RENDER exposes stable A1, A8, XRGB32, and premultiplied ARGB32 formats. The
bounded Picture surface implements `QueryVersion`, `QueryPictFormats`,
`QueryPictIndexValues`, `CreatePicture`, `ChangePicture`,
`SetPictureClipRectangles`, `FreePicture`, `Composite`, `FillRectangles`, and
`CreateSolidFill`. Only `Src` and `Over`, a drawable or solid source, no mask,
integer coordinates, no transform/repeat, and the documented clip/component
alpha attributes are accepted. Pixel results use the deterministic scalar
premultiplied reference and damage the destination drawable.

COMPOSITE implements version negotiation, window and subtree redirect and
unredirect, and `NameWindowPixmap`. Automatic and single-owner manual
redirection are bookkeeping over Glasswyrm's existing canonical window
storage, not a second compositor. A named pixmap shares the current storage;
unmap and window destruction do not invalidate it, while resize moves the
window to new storage and leaves the old named snapshot valid.

### RANDR 1.3

RANDR reports one server-owned output (`Glasswyrm-1`), one active CRTC, one
current/preferred mode (`1024x768` in the fixed acceptance profile), and that
output as primary. The implemented queries cover version, selection, screen
information and size range, current resources, output information and empty
properties, CRTC information and zero gamma size, and primary output.
`SetCrtcConfig` succeeds only when the request exactly restates the current
mode, origin, rotation, and output set; it never performs a DRM/KMS change.

## Core and window-manager additions

Client-created colormaps are supported for the root TrueColor visual with
`AllocNone`. `CreateColormap`, `FreeColormap`, bounded install/uninstall, and
`ListInstalledColormaps` preserve the existing stateless TrueColor color
queries.

The game profile pre-interns a fixed SDL/GWM atom set and maintains the
documented EWMH root properties, supporting-WM proxy, client lists, explicit
stacking order, and active window. `glasswyrmd` interprets the bounded
`_NET_WM_STATE`, `_NET_ACTIVE_WINDOW`, `_NET_CLOSE_WINDOW`, window-type,
Motif-decoration, bypass-compositor, transient, normal-hint, and WM-hint
inputs. Fullscreen geometry and saved normal geometry travel through the
ordinary lifecycle transaction to `gwm`; borderless windowed state does not
imply fullscreen. `gwcomp` remains the only final composition authority, and
M12 performs no direct scanout.

## Efficient buffer and renderer boundary

Game-compatible published CPU buffers use additive GWIPC API 0.7 eventfd
readiness while API 0.1-0.6 synchronization-none peers remain valid. The
producer signals once after copying frame damage; `gwcomp` consumes exactly
one token before reading and does not acknowledge the frame early.

The scalar scene renderer remains the default and exact reference. A
`render_gl=true` build adds a constrained EGL/GLES 2.0 renderer with bounded
texture caching, damaged texture uploads, scissored redraw, damaged readback,
and an honest GBM/surfaceless/fallback report. Both renderers still produce the
same component-neutral `SoftwareFrame`. The DRM dumb-buffer presenter copies
accumulated damage across its two completed buffer generations and falls back
to a full copy when history or post-VT contents are uncertain.

## Compatibility and proof boundary

Host unit, protocol, pixel, CLI, manifest, and report-validator tests cover the
implemented registry, framing, resource lifetimes, wire encoding, software
pixels, EWMH policy, synchronization, renderer selection, and damage-copy
boundaries. The exact SDL release, official `testdraw2` and `testsprite2`
sources, build switches, commands, and hashes are frozen in
`tests/compat/m12/clients.toml`.

Live acceptance is still pending. It requires the fixed Gentoo VM scenario to
prove the raw and XCB extension probes, MIT-SHM and non-SHM fallback, official
SDL workloads, real input/clipboard/cursor/close, fullscreen restore, software
and GLES equality, DRM screenshot parity, VT and process restart replay,
restoration, cleanup, and archive integrity. See the
[SDL profile](../compatibility/M12_SDL.md).

## Explicitly unsupported

- arbitrary SDL versions, SDL OpenGL/Vulkan renderers, arbitrary games, GTK,
  or Qt;
- XInput2, Shape, full XKB, Xcursor themes, pointer barriers, or hardware
  cursor planes;
- full BIG-REQUESTS, shared MIT-SHM pixmaps, full DAMAGE report levels, or full
  RENDER/COMPOSITE/RANDR;
- RENDER glyphs, transforms, filters, gradients, trapezoids, or arbitrary
  masks, and Composite overlay windows;
- dynamic output changes, hotplug, providers, monitors, leases, multiple
  outputs, multiple workspaces, or scaling;
- DRI2, DRI3, PRESENT, GLX, client EGL/OpenGL contexts, Vulkan, client
  DMA-BUF, explicit GPU fences, zero-copy, or direct scanout; and
- VRR, HDR, or color management.
