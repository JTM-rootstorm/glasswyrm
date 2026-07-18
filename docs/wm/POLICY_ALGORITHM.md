# Milestone 5 Policy Algorithm

Policy evaluation is a pure transformation from complete raw state to a policy
state. Ordered maps and explicit sort tuples prevent wire order or hash-table
insertion order from affecting the result.

## Input validation

One context is required. Root window, workspace, and output IDs are nonzero;
the work area is nonempty, bounded to 16,384 per dimension, and must fit signed
32-bit coordinates.

Every policy window is a unique top-level child of the context root. Window ID
and creation serial are nonzero and unique. Requested dimensions are nonzero,
bounded to 16,384, and form checked signed-coordinate extents. An unmapped
window has map serial zero; a mapped window has a nonzero map serial. Workspace
zero means the context workspace; any other workspace must equal it.

A transient target must exist, must be managed, and cannot be the window
itself. The transient graph must be acyclic. Violations are classified as
invalid context, invalid window, unknown reference, limit, or unsupported
metadata as appropriate.

## Applied state and visibility

Managed state precedence is:

```text
minimized > fullscreen > maximized > normal
```

A managed window is visible when it wants mapping and is not minimized. A
transient is additionally hidden when its parent is hidden. An
override-redirect window ignores managed state hints and is visible solely from
map intent.

Hidden windows have stacking `-1`. Attention is copied to policy output and has
no visual effect.

## Geometry

Managed width and height are clamped to the work area. Fullscreen and maximized
windows fill the work area. Override-redirect windows preserve requested
coordinates and dimensions without work-area clamping.

Mapped, managed, non-transient ordinary, utility, and unknown-type windows
participate in cascade placement. Candidates sort by `(creation_serial,
window_id)`. For zero-based slot `n` and step 32:

```text
x_span = work_width - final_width
y_span = work_height - final_height
x_offset = (n * 32) % (x_span + 1), or 0 when x_span is 0
y_offset = (n * 32) % (y_span + 1), or 0 when y_span is 0
final_x = work_x + x_offset
final_y = work_y + y_offset
```

Managed transient geometry is resolved parent-first. Each transient is centered
over its parent's final rectangle and then clamped into the work area. Nested
transients are supported.

## Decorations and eligibility

Override-redirect and fullscreen windows are never decoration-eligible.
Normal and dialog windows are eligible unless preference is explicitly false.
Utility and unknown-type windows are eligible only when preference is
explicitly true. M5 computes eligibility only; it renders no decoration.

`fullscreen_eligible` is true only for a visible managed fullscreen window,
false for other managed windows, and unknown for override-redirect windows.
`direct_scanout_eligible` is always unknown because final scanout authority
belongs to `gwcomp`.

## Stacking

The common ascending stack key is:

```text
(map_serial, creation_serial, window_id)
```

Visible managed roots sort by this key. Each root is emitted followed by its
visible transient descendants depth-first; siblings use the same key. Visible
override-redirect windows form a top band after every managed window and also
sort by the key. Emitted windows receive contiguous stacking values starting
at zero.

## Focus

Candidates are visible managed windows. Override-redirect windows are never
focused. If any candidate has nonzero focus serial, the greatest tuple wins:

```text
(focus_serial, map_serial, creation_serial, window_id)
```

Otherwise the greatest stack key wins. Removal, unmapping, minimization, or
loss of transient-parent visibility triggers the same deterministic fallback.
There is at most one focused window.

## Output order and hash

The complete output snapshot lists visible windows by ascending stacking, then
hidden windows by ascending window ID.

The diagnostic hash is FNV-1a 64-bit with offset basis
`14695981039346656037` and prime `1099511628211`. It covers the tag
`glasswyrm-policy-v1`, committed generation, little-endian context fields, and
the exact 64-byte encoded policy record for each window in output order. It is
for reproducibility and diagnostics, not security.

The M13 multi-output profile uses the tag `glasswyrm-policy-v3` and retains
the same FNV-1a parameters. After the tag it hashes, without padding:

1. the committed `u64` generation and the context fields in their existing
   little-endian order;
2. a `u32` output count, followed by outputs in ascending output-ID order;
3. a `u32` window-hint count, followed by hints in ascending window-ID order;
4. the same exact 64-byte policy-window records used by v1, in policy output
   order.

Each output contributes its `u64` ID; logical and work rectangles as signed
origin plus unsigned extent; scale numerator and denominator; one-byte
transform, enabled, and primary values; and `u32` flags. Each hint contributes
its `u32` window, `u64` previous and preferred output IDs, and `u32` flags.

When interactive bindings are negotiated with the multi-output profile, the
outer hash also uses `glasswyrm-policy-v3`, then hashes the base v3 hash and
the exact existing v2 binding fields. The v1 and v2 tags and byte streams are
unchanged. Canonical v3 vectors are frozen by `multi-output-policy` tests.
