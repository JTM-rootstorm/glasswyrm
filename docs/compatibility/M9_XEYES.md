# Milestone 9 xeyes profile

Status: accepted for the pinned command below.

Supported target:

- `xeyes` 1.3.1;
- `+shape +render`, which disables Shape and Render use;
- the core filled-ellipse path;
- synthetic `QueryPointer` motion;
- fixed geometry `150x100+32+32` with the black/white colors in the manifest.

Unsupported:

- shaped-window mode;
- Render mode;
- a default extension-enabled command claim;
- another xeyes release without a new audit.

The Gentoo VM builds the verified source and proves the unmodified-client run,
four-position synthetic pointer scenario, exact final frame, normalized trace,
combined-client isolation, restart behavior, and clean process/socket teardown.
