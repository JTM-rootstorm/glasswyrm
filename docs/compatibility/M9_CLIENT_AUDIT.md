# Milestone 9 client audit

Status: source-derived initial audit; live pinned-client traces pending.

Milestone 9 targets only `xeyes` 1.3.1 with Shape and Render disabled,
`xclock` 1.2.0 analog with `-norender -update 0`, and `xclock` 1.2.0
digital with `-brief -twentyfour -norender -update 0` under the fixed realtime
test shim and C locale. This is not a general compatibility claim.

Binary-version evidence, observed request order, normalized traces, and frame
evidence must come from the Gentoo acceptance VM. The official X.Org release
tarballs were downloaded from the URLs frozen in `clients.toml`; their
SHA-256 values are recorded there.

## Source-derived request inventory

| Requests | Client source path | Implemented scope | Evidence status |
|---|---|---|---|
| QueryExtension, ListExtensions | Xlib extension discovery | Shape and Render absent | mandatory source path; trace pending |
| AllocColor, AllocNamedColor, FreeColors, QueryColors, LookupColor | Xt color converters | default colormap | mandatory source path; trace pending |
| OpenFont, QueryFont, CloseFont, QueryTextExtents, ListFonts | Xaw clock setup | built-in `fixed` only | mandatory source path; trace pending |
| QueryPointer, TranslateCoordinates | xeyes polling and Xt coordinates | root/child coordinates and state | mandatory source path; trace pending |
| PolyLine, PolySegment, FillPoly | xclock analog hands and ticks | M9 raster profile | mandatory source path; trace pending |
| PolyFillArc | xeyes eye and pupil fill | full ellipses only | mandatory source path; trace pending |
| ImageText8, PolyText8 | xclock digital | built-in fixed glyphs | mandatory source path; trace pending |
| GetKeyboardMapping, GetModifierMapping, GetPointerMapping | Xlib/Xt startup | fixed M9 maps | mandatory source path; trace pending |

Requests outside this inventory require a pinned trace and a source call path.
Each row must gain exact test references and VM evidence before completion.

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
