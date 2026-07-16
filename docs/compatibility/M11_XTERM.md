# xterm Patch 410 Compatibility Profile

Status: validated for the exact profile below.

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
observed request set, normalized trace, and evidence gates. This narrow result
must not be generalized to another xterm build or invocation.
