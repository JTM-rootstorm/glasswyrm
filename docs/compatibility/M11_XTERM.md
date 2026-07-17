# xterm Patch 410 Compatibility Profile

Status: validated by two deterministic full bootstrap captures for the exact
profile below and reproduced by a focused exact diagnostic rerun; the final
full exact-match VM gate is pending.

The only Milestone 11 xterm target is unmodified patch 410 built and launched
with the exact profile pinned by `tests/compat/m11/clients.toml`. It uses core
bitmap fonts, ASCII/C locale behavior, a real PTY and Bash, and disables Xft,
wide characters, luit, toolbar, sixel, ReGIS, Xinerama, input methods, and
themed Xcursor use.

The evidence-backed candidate claim is limited to:

- one US `pc105` keymap through explicit libinput keyboard/mouse path devices;
- one workspace and single DRM/KMS output;
- software cursor;
- the core Xaw scrollbar thumb's depth-1 opaque-stippled rectangle fill;
- PRIMARY and minimum CLIPBOARD exchange;
- Alt+Button1 move, Alt+Button3 resize, and Alt+F4 close.

It does not cover a distro-default xterm, Xft/Unicode mode, the XKB extension,
XIM/compose, arbitrary layouts, themed/hardware cursors, `UngrabButton`,
passive key grabs, complete grabs, a desktop clipboard manager, decorations,
or multiple workspaces/outputs. The only passive-grab request in the accepted
profile is `GrabButton`.

See [the source and trace audit](M11_XTERM_AUDIT.md) for the pinned source,
observed request set, normalized trace, and evidence gates. Captures A and B at
`eb8a20f76b24cc7c07459a402603bad5e7b6cc39` produced byte-identical normalized
traces with SHA-256
`702e014eb33d67f9fa719ff0f133782b950a83a153c882394f53d8606f229834`
and passed every runtime evidence gate except the unavailable exact-fixture
comparison. The fixture presence-normalizes only `ClearArea`, `CopyArea`,
`ImageText8`, `PolyLine`, and `PutImage`; the archived raw traces retain exact
drawing counts, while protocol, selection, and event behavior stays exact. A
focused exact rerun passed but is diagnostic-only. Final acceptance requires
the complete clean VM route to reproduce the committed fixture with
`exact_trace=passed`. This narrow result must not be generalized to another
xterm build or invocation.
