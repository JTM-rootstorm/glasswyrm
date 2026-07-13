# Milestone 9 request profile

Status: accepted for the pinned Milestone 9 client commands.

New implemented groups are extension discovery; default-colormap color
queries; fixed core fonts and text; pointer/coordinate queries; fixed keyboard,
modifier, and pointer mappings; depth-1 pixmaps and XYBitmap upload; one-pixel
lines and segments; convex fill; and complete filled ellipses. Child
InputOutput windows are flattened into their managed top-level buffer.

The exact request subsets and errors are normative in
[the protocol note](../protocols/x11-milestone-9.md). The audit table explains
why each group is in scope. The reviewed traces define the observed profile and
prove trace-gated contingency requests without broadening the compatibility
claim.

Compatibility is limited to the commands in `tests/compat/m9/clients.toml`.
That manifest pins the official release hashes. Reviewed trace and frame
fixtures under `tests/fixtures/m9/` and the Gentoo VM acceptance prove the
documented end-to-end profiles.
