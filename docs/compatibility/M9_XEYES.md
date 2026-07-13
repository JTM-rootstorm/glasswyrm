# Milestone 9 xeyes profile

Status: protocol foundation implemented; external acceptance pending.

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

The server-side requests, depth-1 icon storage, child composition, ellipse
raster, pointer reply, trace recorder, and bounded process harness exist. A
successful unmodified-client run, normalized trace, synthetic pointer frame,
source hash, and cleanup proof have not yet been frozen as acceptance evidence.
