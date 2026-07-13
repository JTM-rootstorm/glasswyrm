# Milestone 9 client audit

Status: accepted upstream-source audit and reviewed Glasswyrm traces.

Milestone 9 targets only `xeyes` 1.3.1 with Shape and Render disabled,
`xclock` 1.2.0 analog with `-norender -update 0`, and `xclock` 1.2.0
digital with `-brief -twentyfour -norender -update 0` under the fixed realtime
test shim and C locale. This is not a general compatibility claim.

The extracted official sources were configured and built locally beneath
`/tmp/glasswyrm-m9-clients`. The installed binaries report exactly
`xeyes 1.3.1` and `xclock 1.2.0` for `-version`. Their `-help` output accepts
the manifest options, including xeyes `+shape`, `+render`, and xclock
`-analog`, `-digital`, `-brief`, `-twentyfour`, `-norender`, and `-update`.
The accepted Gentoo runtime rebuilds those verified sources and freezes the
observed request order, normalized traces, and exact frame evidence.

## Direct application call paths

Paths below are relative to the extracted release root. “Toolkit-mediated”
means the application source establishes the named Xt/Xaw path but does not
call the core Xlib request wrapper directly; the normalized trace must prove
the exact wire request.

| Opcode / request | Application source path | Required semantics | Evidence |
|---|---|---|---|
| 1 `CreateWindow` | xeyes `xeyes.c:169-170`, `main` -> `XtCreateManagedWidget` -> `XtRealizeWidget`; `Eyes.c:879-889`, `Realize` -> `XtCreateWindow`; xclock `xclock.c:236-237`, `main` -> `XtCreateManagedWidget` -> `XtRealizeWidget` | top-level and child InputOutput windows | source and reviewed traces verified |
| 9/11 `MapSubwindows` / `UnmapSubwindows` | No direct call in either release; retained as mandatory child-lifecycle infrastructure for toolkit-managed descendants | immediate children using local map semantics | `MapSubwindows` observed; `UnmapSubwindows` remains tested contingency behavior |
| 38 `QueryPointer` | xeyes `Eyes.c:770-784`, `draw_it_core` -> `XQueryPointer` | recurring synthetic pointer and child-relative coordinates | source and reviewed recurring trace verified |
| 40 `TranslateCoordinates` | xeyes `Eyes.c:668-682`, `computePupils` when `-distance` is enabled | window-to-root translation | direct source verified; target command does not enable `-distance` |
| 45/46 `OpenFont` / `CloseFont` | xclock `Clock.c:236-240` fixed `XtRFontStruct` resource; `Clock.c:853-863`, `Initialize`; resource conversion and teardown are toolkit-mediated | built-in `fixed` resource lifetime | `OpenFont` observed; teardown remains tested contingency behavior |
| 47 `QueryFont` | xclock `Clock.c:970-978`, `Initialize` -> `XQueryFont` fallback, and the `XTextWidth` metric path | deterministic fixed metrics for font or default GC | source and reviewed trace verified |
| 48 `QueryTextExtents` | xclock `Clock.c:970-978` and `1819-1829`, `Initialize`/`clock_tic` -> `XTextWidth`, whose Xlib metric source may use cached `QueryFont` data | fixed-font extents | source anchor verified; exact request trace-gated |
| 49 `ListFonts` | xclock fixed-font Xt conversion rooted at `Clock.c:236-240`; no direct `XListFonts` call exists in the application release | bounded fixed-name lookup | toolkit-mediated path observed in reviewed traces |
| 65 `PolyLine` | xclock `Clock.c:1880-1988`, `clock_tic` and `erase_hands` -> `XDrawLines` | hand outlines/erasure, origin coordinates | source and reviewed analog trace verified |
| 66 `PolySegment` | xclock `Clock.c:2188-2207`, `DrawClockFace` -> `XDrawSegments` | independent face ticks | source and reviewed analog trace verified |
| 69 `FillPoly` | xclock `Clock.c:1880-1988`, `clock_tic` and `erase_hands` -> `XFillPolygon(..., Convex, CoordModeOrigin)` | convex hand interiors/erasure | source and reviewed analog trace verified |
| 71 `PolyFillArc` | xeyes `Eyes.c:502-578`, `drawEllipse` -> `XFillArc(..., 90*64, 360*64)` | complete filled ellipses | source and reviewed xeyes trace verified |
| 74 `PolyText8` | No direct caller in the selected xclock digital path; Xaw/Xlib may select core text encodings internally | bounded fixed text items | trace-gated within mandatory implementation |
| 76 `ImageText8` | xclock `Clock.c:1684-1831`, `clock_tic` -> `XDrawImageString` | fixed digital text and cell background | source and reviewed digital trace verified |
| 84/85 `AllocColor` / `AllocNamedColor` | xeyes `Eyes.c:59-72` pixel resources and xclock `Clock.c` pixel resources are converted during `XtAppInitialize`/`XtOpenApplication`; wrappers are inside libXt/libXmu | default-colormap Xt string conversion | `AllocNamedColor` observed; `AllocColor` remains tested contingency behavior |
| 88 `FreeColors` | Xt pixel-resource teardown from the same resource paths | validated TrueColor no-op | not observed; retained as tested teardown behavior |
| 91 `QueryColors` | xeyes `Eyes.c:476-487`, `Initialize` -> `XQueryColor` for compiled Render color setup | exact default-colormap RGB | source and reviewed xeyes trace verified |
| 92 `LookupColor` | Xt color conversion rooted at the xeyes/xclock pixel resource tables | bounded names and hexadecimal forms | toolkit-mediated path observed in reviewed xclock traces |
| 98 `QueryExtension` | xeyes `Eyes.c:183-240`, `CheckPresent` queries XFIXES, DAMAGE, and Present through XCB; `Eyes.c:377-390`, `has_xi2` -> `XIQueryVersion`; `Eyes.c:466-469` optionally checks Shape; xclock `Clock.c:1021-1031`, `Initialize` -> `XRenderQueryVersion` | every extension absent | discovery paths and reviewed names/order verified |
| 99 `ListExtensions` | No direct call in either application release | empty extension list | not observed; retained as tested discovery fallback |
| 101 `GetKeyboardMapping` | startup begins at xeyes `xeyes.c:134`, `XtAppInitialize`, and xclock `xclock.c:196`, `XtOpenApplication`; keyboard initialization is inside libXt/Xlib | fixed two-keysym core map | not observed with the accepted client library build; directly tested |
| 117 `GetPointerMapping` | same Xt application initialization anchors | buttons 1 through 5 | not observed with the accepted client library build; directly tested |
| 119 `GetModifierMapping` | same Xt application initialization anchors | fixed M8 modifier map | not observed with the accepted client library build; directly tested |
| 53/72 `CreatePixmap` + `PutImage` | xeyes `xeyes.c:156-165` and xclock `xclock.c:217-233`, `main` -> `XCreateBitmapFromData` twice for icon and mask | depth-1 bitmap pixmaps and upload | direct source verified; live trace proved the narrow XYPixmap exception in `0efe792` |
| 55/56 `CreateGC` / `ChangeGC` | xeyes `Eyes.c:404-458`, `Initialize` -> `XtGetGC`; xclock `Clock.c:853-1012`, `Initialize` -> `XtGetGC` with foreground, background, font, line width, and graphics-exposures values | atomic M9 GC subset | `CreateGC` observed; source and contingency tests cover changes |
| 61/62/70 `ClearArea` / `CopyArea` / `PolyFillRectangle` | xclock `Clock.c:1819-1834`, `clock_tic` -> `XClearArea`; xeyes `Eyes.c:502-515`, `drawEllipse` clear path -> `XFillRectangle`; Render buffering contains `XCopyArea` but is disabled by the target profile | supported child drawables | `ClearArea` and fill observed; disabled `CopyArea` path remains tested contingency behavior |

### Extension-discovery correction

The locally built xeyes has both `PRESENT` and `XRENDER` compiled in. Even with
`+shape +render`, `Initialize` calls `has_xi2` and `CheckPresent`; the latter
prefetches XFIXES, DAMAGE, and Present extension data. The selected source can
therefore issue more `QueryExtension` names than only Shape and Render. Because
Glasswyrm reports all extensions absent, so no extension-specific request
follows. The reviewed traces freeze the query names and order.

### `-update 0` correction

The xclock command accepts `-update 0`, but `Clock.c:1015-1018`, `Initialize`,
changes any update below `0.001` to `60`. Later `Clock.c:1684-1702`,
`clock_tic`, schedules a timeout when `update` is nonzero. Thus `-update 0`
does **not** mean “never schedule another update” in xclock 1.2.0; it produces
the initial fixed-time frame and then uses a 60-second interval. Acceptance
must terminate the client before that interval or keep the fixed-time shim
active. This does not alter the exact command profile.

## Trace collection

`glasswyrmd --x11-trace PATH` requires a nonexistent path, rejects symlinks and
replacement targets, and initializes before opening the X11 listener. Raw JSONL
omits authorization, property, image, and text payloads. Normalize it with:

```sh
tests/compat/m9/m9_trace_summarize raw.trace.jsonl >normalized.trace.json
```

Only reviewed normalized summaries belong in `tests/fixtures/m9/`; bounded raw
traces remain VM diagnostic artifacts.

## Known differences from Xorg

- Shape and Render are intentionally absent.
- Only the default colormap and built-in fixed font are in scope.
- Locale font sets, Xft, XKB/chime behavior, and arbitrary fonts are deferred.
- Default extension-enabled application invocations are unsupported.
