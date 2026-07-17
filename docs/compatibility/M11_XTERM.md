# xterm Patch 410 Compatibility Profile

Status: accepted for the exact profile below by the clean full exact-match VM
run at `53ec4879b858b96a9b7e8734fb173d037cbc683b`.

The only Milestone 11 xterm target is unmodified patch 410 built and launched
with the exact profile pinned by `tests/compat/m11/clients.toml`. It uses core
bitmap fonts, ASCII/C locale behavior, a real PTY and Bash, and disables Xft,
wide characters, luit, toolbar, sixel, ReGIS, Xinerama, input methods, and
themed Xcursor use.

The accepted claim is limited to:

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
and selected the finalized fixture. The fixture presence-normalizes only
`ClearArea`, `CopyArea`,
`ImageText8`, `PolyLine`, and `PutImage`; the archived raw traces retain exact
drawing counts, while protocol, selection, and event behavior stays exact. A
focused exact rerun passed but remained diagnostic-only. The later clean full
run at `53ec4879b858b96a9b7e8734fb173d037cbc683b` reproduced the committed
fixture with `exact_trace=passed`, `passed=true`, and no evidence errors. The
repository release sequence still repeats host and clean VM gates at the final
documentation HEAD, but this narrow compatibility result is accepted and must
not be generalized to another xterm build or invocation.
