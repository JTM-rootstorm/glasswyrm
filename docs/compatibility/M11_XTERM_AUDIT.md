# Milestone 11 xterm source audit

Status: official patch 410 source, signature, build profile, and launch profile
pinned; Glasswyrm live traces and runtime acceptance pending.

This document records source-audit facts only. It does not claim that xterm
runs on Glasswyrm, that the requests below are implemented, or that any M11
trace or screenshot fixture has been accepted. Those claims require the fixed
live trace, interactive tests, and DRM acceptance described by the Milestone 11
plan.

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
the behavior, but only a reviewed wire trace can freeze their exact order and
temporary atoms or properties.

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

## Grabs and keyboard discovery

No direct calls to `XGrabPointer`, `XUngrabPointer`,
`XChangeActivePointerGrab`, `XAllowEvents`, or `XQueryKeymap` occur in the
patch 410 source. The ordinary selection drag can use the core automatic
button grab after delivery of `ButtonPress`.

Passive grabs remain a likely startup requirement. `menu.c:3063`
(`SetupMenus`) always registers `HandlePopupMenu` with `XtRegisterGrabAction`,
even when the toolbar is disabled. The default translations contain
Ctrl+Button1, Ctrl+Button2, and Ctrl+Button3 popup actions. Xt may install
`GrabButton` requests while realizing those translations. `GrabButton` and
`UngrabButton` must remain trace-gated, but should be expected in the first
live capture rather than assumed absent.

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
top-level shell. This source path does not prove that Glasswyrm currently does
so.

## Pending evidence

Before this audit can become an accepted compatibility claim:

1. build patch 410 from the verified archive in the fixed Gentoo guest;
2. record the exact configure summary and `XTerm(410)` output;
3. collect and normalize startup, typing, scrolling, selection, paste, move,
   resize, and close traces from Glasswyrm;
4. resolve every toolkit-mediated or trace-gated request from those traces;
5. run the two-xterm PRIMARY and CLIPBOARD scenarios;
6. capture and review the required DRM screenshots; and
7. add fixtures only after review.

No trace fixture or runtime result is asserted by this source-audit document.
