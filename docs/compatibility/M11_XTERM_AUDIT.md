# Milestone 11 xterm source audit

Status: accepted for the pinned patch 410 profile. The clean full VM run at
`53ec4879b858b96a9b7e8734fb173d037cbc683b` reproduced the finalized
presence-normalized fixture exactly and passed every runtime evidence gate.

This document combines the source audit, reviewed fixture-selection captures,
and accepted exact-match evidence. The compatibility claim remains limited to
the exact profile below. Release completion still repeats the host matrix and
clean M10/M11 sequence at the final documentation commit; that release step is
outside the already accepted client-profile evidence.

## Verified release identity

The pinned release is the unmodified official xterm patch 410 archive:

- archive: `https://invisible-island.net/archives/xterm/xterm-410.tgz`
- archive size: 1,627,227 bytes
- archive SHA-256:
  `7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f`
- detached signature:
  `https://invisible-island.net/archives/xterm/xterm-410.tgz.asc`
- signature SHA-256:
  `f1acdd6a4516417b3a5149609ac6bd9b36aff6cc4b965dea95cd780d64ec98ce`
- upstream key:
  `https://invisible-island.net/public/dickey%40invisible-island.net-rsa3072.asc`
- key SHA-256:
  `eec7eccb51a27ae633784d1b1ef42eb775130c782ea51a6c47fa7a901484d6db`
- key fingerprint: `19882D92DDA4C400C22C0D56CC2AF4472167BE03`

The detached RSA3072/SHA-512 signature was created at
`2026-05-01T23:56:20Z`. Verification in an isolated GnuPG keyring produced
`GOODSIG` and `VALIDSIG` for the fingerprint published on Thomas E. Dickey's
official HTTPS key page. The archive hash above was computed locally from that
signed official artifact; upstream does not publish a separate SHA-256 file in
the archive index.

`MANIFEST` identifies `xterm-410`, and `version.h` defines patch 410 with date
2026-05-01. The archive is CVS-derived and contains per-file CVS identifiers,
not a repository snapshot commit. No Git commit is recorded for this pin. A
local build of the unmodified source reports `XTerm(410)`.

The machine-readable pin and launch arrays are in
`tests/compat/m11/clients.toml`.

## Verified build profile

The following configure arguments were accepted by the patch 410 release:

```text
--enable-openpty
--disable-freetype
--disable-wide-chars
--disable-luit
--disable-toolbar
--disable-sixel-graphics
--disable-regis-graphics
--without-xinerama
--disable-xcursor
--disable-tek4014
--disable-session-mgt
--disable-input-method
--disable-active-icon
--disable-desktop
--disable-print-graphics
--disable-screen-dumps
```

Gentoo's ncurses installation splits the termcap compatibility symbols into
`libtinfo`. The VM therefore invokes this unchanged configure profile as
`LIBS=-ltinfo ./configure ...`; the explicit library fixes platform linkage
only and does not enable another xterm feature.

The configure result uses `openpty` from libutil and retains core Xaw, Xmu,
Xt, Xlib, and bitmap-font rendering. It omits FreeType/Xft, wide characters,
luit, the toolbar, ReGIS, sixel, Xinerama, Xcursor themes, Tektronix emulation,
session management, and input methods.

Patch 410 has no configure option which disables XKB. The verified build still
detects `XkbKeycodeToKeysym`, `XkbQueryExtension`, and the XKB bell API.
Glasswyrm must therefore tolerate XKB discovery and preserve the documented
absent-extension fallback. A nonexistent `--disable-xkb` option must not be
added to the manifest.

## Fixed launch profile

The launch contract is the exact environment and argv array in
`tests/compat/m11/clients.toml`. It selects the `fixed` core normal and bold
fonts, keeps the compile-time non-wide-character mode, disables cursor
blinking, and starts Bash with
`tests/compat/m11/m11-bashrc`. The Bash profile fixes the locale and prompts,
disables command history and bracketed-paste negotiation, and emits no title
escape sequence.

The non-wide-character binary does not expose xterm's `-u8` runtime option.
Passing `-u8 0` to that build is therefore invalid and redundant: Unicode
support is already absent at compile time. The fixed argv intentionally omits
that option while retaining the plan's ASCII-only compatibility boundary.

The draft plan combined Bash's `--norc` and `--rcfile` options. Direct
validation showed that `--norc` also suppresses the explicitly named rcfile,
leaving the default prompt and locale in place. The pinned argv therefore uses
`--noprofile --rcfile FILE` without `--norc`. An explicit rcfile replaces the
usual personal bashrc, so the corrected form remains isolated while actually
loading the deterministic profile.

The target remains limited to patch 410 under this profile. It is not a claim
for a distribution's default xterm, Xft, Unicode, themed cursors, the toolbar,
or another terminal emulator.

## Reviewed deterministic fixture and acceptance evidence

Two complete VM runs, fixture captures A and B, used the same tested, full
build, and runtime commit:

```text
eb8a20f76b24cc7c07459a402603bad5e7b6cc39
```

Both runs completed the full interactive scenario with `scenario_exit=0`.
Every mandatory summary field passed except `exact_trace`, which correctly
reported `bootstrap` because no committed fixture existed. Normalizing both
archived raw traces with the finalized presence-normalization rules reproduces
the committed fixture byte-for-byte at SHA-256:

```text
702e014eb33d67f9fa719ff0f133782b950a83a153c882394f53d8606f229834
```

The raw JSONL traces remain exact and checksum-protected in their evidence
archives. Runtime redraw scheduling produced 800 or 806 `ImageText8` requests
and 6 or 7 `PolyLine` requests across the reviewed raw captures. Those raw
counts are evidence, not a compatibility requirement. The fixture therefore
presence-normalizes the five drawing primitives `ClearArea`, `CopyArea`,
`ImageText8`, `PolyLine`, and `PutImage` to one occurrence in both its request
and opcode histograms. Request presence, first-occurrence order, all protocol
and selection counts, and the complete event sequence remain exact.

The exact presence-normalized fixture request histogram is:

```text
AllocColor=424                 ChangeProperty=38
ChangeWindowAttributes=151     ClearArea=1
CloseFont=3                    ConfigureWindow=6
ConvertSelection=3             CopyArea=1
CreateGC=19                    CreateGlyphCursor=17
CreatePixmap=6                 CreateWindow=6
DeleteProperty=2               FreeCursor=1
FreeGC=6                       GetGeometry=5
GetInputFocus=17               GetKeyboardMapping=6
GetModifierMapping=6           GetProperty=20
GetSelectionOwner=4            GrabButton=96
ImageText8=1                   InternAtom=97
MapSubwindows=2                MapWindow=2
OpenFont=9                     PolyLine=1
PutImage=1                     QueryColors=5
QueryExtension=6               QueryFont=6
QueryTree=3                    RecolorCursor=5
SendEvent=3                    SetSelectionOwner=3
```

There were no protocol errors, recurring requests, or unknown opcodes. The
only trace-gated request was exactly 96 `GrabButton` requests; no
`UngrabButton` or passive key-grab request was observed. The trace recorded
five application connections and a maximum request length of 9,240 bytes.

The corresponding presence-normalized opcode counts are one each for
`ClearArea` (61), `CopyArea` (62), `PolyLine` (65), `PutImage` (72), and
`ImageText8` (76). All other opcode counts remain exact.

The subsequent clean full exact run used the same pinned source and profile at
tested, full-build, and runtime commit:

```text
53ec4879b858b96a9b7e8734fb173d037cbc683b
```

It reproduced the committed fixture byte-for-byte at SHA-256
`702e014eb33d67f9fa719ff0f133782b950a83a153c882394f53d8606f229834`.
Its summary reports `passed=true`, `scenario_exit=0`,
`exact_trace=passed`, and an empty `evidence_errors` list. The run repeated all
interactive, VT/restart, screenshot, restoration, cleanup, and archive gates
described below; this is the accepted compatibility result. The earlier
focused exact rerun remains diagnostic-only and is not the basis of acceptance.

The exact event histogram is:

```text
KeyPress(2)=256                KeyRelease(3)=255
ButtonPress(4)=13              ButtonRelease(5)=13
MotionNotify(6)=3              FocusIn(9)=3
FocusOut(10)=2                 Expose(12)=6
NoExposure(14)=30              MapNotify(19)=4
ConfigureNotify(22)=14         PropertyNotify(28)=40
SelectionClear(29)=1           SelectionRequest(30)=3
SelectionNotify(31)=3          ClientMessage(33)=1
```

The reviewed selection path contains `GetSelectionOwner`,
`SetSelectionOwner`, `ConvertSelection`, and `SendEvent`. The live CLIPBOARD
probe returned `TARGETS` and `UTF8_STRING`, transferred
`M11_CLIPBOARD_TOKEN`, and replaced PRIMARY, producing the single
`SelectionClear`. The single `ClientMessage` is the format-32
`WM_PROTOCOLS`/`WM_DELETE_WINDOW` close path.

The PTY transcript proves corrected typing, six server-generated repeat
characters, deterministic sequence output, exact PRIMARY paste into the second
xterm, and the `M11_VT` and `M11_RESTART` post-transition commands. The uinput
scenario and acceptance evidence separately prove vertical and horizontal
wheel scrolling. XCB geometry, ConfigureNotify, GWM policy, Expose, and PTY
evidence agree on the second xterm's exact transition:

```text
80x24: 484x316+384+160
move:  484x316+480+224  (+96,+64)
90x28: 544x368+480+224
```

The cursor journal accepted and scanned out left-pointer, xterm-text,
fleur-move, and bottom-right-resize presentations, including buffer reuse;
the hidden-glyph typing cursor was also exercised. GWM replay preserved
Button1 move, Button3 resize, Alt+F4 (`0xffc1`), and the 96-by-64 minimum.

Both runs passed VT suspension/no-delivery/resume, post-VT input, GWM replay,
compositor replay, live xterm survival, post-restart input, KMS/KD/VT/getty
restoration, service shutdown, socket/device cleanup, and evidence-archive
validation. The final canonical mirror and graphical-console screenshot were
byte-identical with SHA-256:

```text
bd6a1e885c0a633d42f5b61af0e8f355df2ace8fb6a39bb6f4b744c1bd317cdc
```

## Selection paths

Paths are relative to the extracted patch 410 source root.

- `charproc.c:15048-15054` defines the default pointer translations for
  selection start, extension, completion, and middle-button insertion.
- `button.c:2989` (`do_select_start`), `button.c:3013`
  (`HandleSelectStart`), `button.c:1552` (`HandleSelectExtend`), and
  `button.c:1604` (`do_select_end`) implement the gesture.
- `button.c:5084` (`_OwnSelection`) uses `XtOwnSelection` and
  `XtDisownSelection`. `ConvertSelection`, `LoseSelection`, and
  `SelectionDone` are at lines 4827, 5033, and 5075.
- `button.c:2228` (`xtermGetSelection`) uses `XtGetSelectionValue`;
  `SelectionReceived` at line 2736 accepts the result and writes it to the
  pseudo-terminal.
- The non-wide-character target list in `button.c:2017`
  (`alloc8bitTargets`) can still advertise `UTF8_STRING` when the X libraries
  provide it. In the C locale the accepted test content remains ASCII.

The resulting `SetSelectionOwner`, `ConvertSelection`, property, selection
event, and `SendEvent` requests are mediated by Xt. Source inspection anchors
the behavior; the accepted normalized trace above freezes their exact order
and temporary atoms or properties for this profile.

## Pointer and cursor paths

The normal pointer is not a pixmap cursor in this profile:

- `misc.c:990` (`xtermSetupPointer`) selects the default `XC_xterm` shape.
- `misc.c:836` (`make_colored_cursor`) calls `XCreateFontCursor` and then the
  recolor path; Xlib normally realizes that as cursor-font open,
  `CreateGlyphCursor`, and font close requests.
- `util.c:2941` calls `XRecolorCursor`, and `misc.c:1021` applies the cursor
  with `XDefineCursor`.
- Patch 410 also creates the cursor-font `XC_sb_h_double_arrow` and
  `XC_sb_v_double_arrow` pairs plus the up, down, left, and right scrollbar
  arrow pairs during Xaw widget startup, even with the fixed no-toolbar
  profile. The accepted bounded glyph subset therefore includes source/mask
  pairs 106/107, 108/109, 110/111, 112/113, 114/115, and 116/117.

There is a second, easy-to-miss cursor path. The documented default
`pointerMode` is 1, so typing may hide the pointer. `misc.c:700`
(`make_hidden_cursor`) opens `nil2`, falls back to `fixed`, and calls
`XCreateGlyphCursor` with source character `X` and mask character space. The
hidden cursor is freed at `charproc.c:12211`.

Consequently, supporting only cursor-font shape/mask pairs, including
`XC_xterm`, is insufficient for the exact launch contract. Glasswyrm must also
support this bounded fixed-font glyph cursor path, or the launch contract would
have to change to force `pointerMode` 0. This audit does not make that change.

The pointer cursor is applied to both shell and terminal child windows. The
effective-cursor implementation therefore needs child-aware hit testing and
window-ancestor inheritance.

## Xaw scrollbar stipple

The fixed profile still constructs an Xaw Scrollbar widget. In libXaw 1.0.16,
`Scrollbar.c:471-500` creates the default depth-1 thumb with
`XmuCreateStippledPixmap`, then requests a GC with `FillOpaqueStippled` and
`GCStipple`. Xt omits the default foreground from the wire request, leaving the
observed value mask `0x908`: background, fill style, and stipple, with values
white, `3`, and the generated depth-1 pixmap XID. `Scrollbar.c:390-399` uses
that GC with `XFillRectangle` to paint the thumb.

Glasswyrm consequently retains the bitmap storage through the GC even if the
pixmap XID is freed, and implements the drawable-origin opaque-stipple pattern
only for `PolyFillRectangle`. Other patterned drawing remains outside this
bounded xterm requirement and fails explicitly rather than being rendered as a
solid foreground fill.

The live startup trace also records request 316 creating GC `0x200015` on the
terminal child window `0x200010` with value mask `0x4008` (white background and
font `0x20000c`). That drawable is a nested depth-24 `InputOutput` window, not a
root child. Glasswyrm therefore accepts GC creation on any valid depth-24
`InputOutput` window; `InputOnly` windows and incompatible window depths remain
`BadMatch`.

The startup upload is a depth-1 `ZPixmap`, not an `XYBitmap`: the accepted
trace records format 2, depth 1, and a 48-by-48 image. Glasswyrm therefore
advertises the matching depth-1 pixmap format and allowed screen depth, then
decodes its one-bit pixels using the setup LSBFirst bitmap order and 32-bit row
padding. Focused little- and big-endian request tests prove that client byte
order affects the request header only, while the image bits remain LSBFirst.

## Grabs and keyboard discovery

No direct calls to `XGrabPointer`, `XUngrabPointer`,
`XChangeActivePointerGrab`, `XAllowEvents`, or `XQueryKeymap` occur in the
patch 410 source. The ordinary selection drag can use the core automatic
button grab after delivery of `ButtonPress`.

Passive grabs remain a likely startup requirement. `menu.c:3063`
(`SetupMenus`) always registers `HandlePopupMenu` with `XtRegisterGrabAction`,
even when the toolbar is disabled. The default translations contain
Ctrl+Button1, Ctrl+Button2, and Ctrl+Button3 popup actions. Xt may install
`GrabButton` requests while realizing those translations. The reviewed profile
contains exactly 96 `GrabButton` requests and no `UngrabButton`; only the
observed `GrabButton` path is accepted.

The only direct keyboard-grab path is the secure-keyboard menu action in
`menu.c:1018`; ordinary acceptance should not require it unless the trace says
otherwise. Startup modifier discovery in `input.c:2159` (`VTInitModifiers`)
uses `XGetModifierMapping`, `XDisplayKeycodes`, and `XGetKeyboardMapping`.
Keyboard LED/reset helpers use `XGetKeyboardControl` and
`XChangeKeyboardControl`. XKB-aware key and bell helpers may first query the
absent XKB extension and then fall back to core behavior.

## WM close path

`main.c:4256` interns `WM_DELETE_WINDOW`. `charproc.c:9930` (`VTInit`)
realizes the shell and installs it with `XSetWMProtocols`; the translation at
`charproc.c:9925` maps the `WM_PROTOCOLS` client message to `DeleteWindow`.
`main.c:1893` implements that action through the normal hangup path.

The Milestone 11 close binding must therefore send one format-32
`WM_PROTOCOLS` `ClientMessage` containing `WM_DELETE_WINDOW` to the managed
top-level shell. The source path alone does not prove delivery; the accepted
trace's single `ClientMessage` and clean first-xterm shutdown provide the
matching live evidence.

## Accepted compatibility boundary

The A/B bootstrap evidence selected the committed fixture, and the clean full
run at `53ec4879b858b96a9b7e8734fb173d037cbc683b` accepted it with
`exact_trace=passed`. This establishes only the xterm patch 410 core-font ASCII
profile documented here. It does not broaden the claim to another xterm build,
toolkit, extension, keyboard model, output topology, or input profile.

Before the branch is released, the repository-level final sequence still
repeats the host compiler/sanitizer and component gates plus the historical
clean-snapshot M10/M11 VM order at the final documentation HEAD. That
release-engineering repetition does not make this compatibility claim
provisional.
