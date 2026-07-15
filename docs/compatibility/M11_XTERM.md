# xterm Patch 410 Compatibility Profile

Status: implementation present; live trace, interaction, VM, and screenshot
acceptance pending.

The only Milestone 11 xterm target is unmodified patch 410 built and launched
with the exact profile pinned by `tests/compat/m11/clients.toml`. It uses core
bitmap fonts, ASCII/C locale behavior, a real PTY and Bash, and disables Xft,
wide characters, luit, toolbar, sixel, ReGIS, Xinerama, input methods, and
themed Xcursor use.

Once the pending gate passes, the claim is limited to:

- one US `pc105` keymap through explicit libinput keyboard/mouse path devices;
- one workspace and single DRM/KMS output;
- software cursor;
- PRIMARY and minimum CLIPBOARD exchange;
- Alt+Button1 move, Alt+Button3 resize, and Alt+F4 close.

It does not cover a distro-default xterm, Xft/Unicode mode, the XKB extension,
XIM/compose, arbitrary layouts, themed/hardware cursors, complete grabs, a
desktop clipboard manager, decorations, or multiple workspaces/outputs.

See [the source and trace audit](M11_XTERM_AUDIT.md). Until its pending-evidence
section is satisfied, do not describe xterm as validated on Glasswyrm.
