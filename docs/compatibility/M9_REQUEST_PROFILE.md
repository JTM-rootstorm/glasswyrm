# Milestone 9 request profile

Status: implemented request foundation; pinned-client traces pending.

New implemented groups are extension discovery; default-colormap color
queries; fixed core fonts and text; pointer/coordinate queries; fixed keyboard,
modifier, and pointer mappings; depth-1 pixmaps and XYBitmap upload; one-pixel
lines and segments; convex fill; and complete filled ellipses. Child
InputOutput windows are flattened into their managed top-level buffer.

The exact request subsets and errors are normative in
[the protocol note](../protocols/x11-milestone-9.md). The audit table explains
why each group is in scope. Frozen traces may narrow this profile or prove a
contingency request, but must not silently broaden it.

Compatibility is limited to the commands in `tests/compat/m9/clients.toml`.
That manifest still contains `PENDING` source hashes, and the repository does
not yet contain reviewed M9 trace/frame fixtures. Therefore this document does
not declare that xeyes or xclock works end to end.
