# Milestone 13 multi-output rendering

An output-model scene owns a complete map of outputs and one explicit primary
output and membership set for each nonmetadata surface and cursor. `gwcomp`
validates this complete snapshot against its backend inventory before rendering
or changing committed state.

The renderer returns a `SoftwareFrameSet`: a deterministic output-ID-keyed set
of native-sized frames, physical damage, visible hashes, layout generation,
commit generation, and transaction ordinal. No root-sized intermediate
framebuffer is created. One acknowledgement covers the entire frame set.

## Software path

For each enabled output, the canonical renderer preserves compatible pixels
outside damage, clears physical damage, walks the global stack, filters by
surface membership and logical intersection, maps physical pixel centers back
through transform and rational scale, samples the client buffer using its
integer scale, composites premultiplied alpha, and renders the cursor last.

A surface that spans an output boundary is rendered independently into every
member output. A disabled output has no frame. Output enable, mode, scale, or
transform changes fully damage that output; logical movement and surface state
changes damage the old and new physical bounds on each affected output.

## GLES path

The optional GLES renderer maintains framebuffer and texture state per stable
output ID. Native physical dimensions define its viewport, physical damage
defines its scissor, and its transform and texture-coordinate mapping follow
the software reference. Targets are removed when outputs disappear, while
compatible output pixels remain preserved outside damage.

Renderer selection is atomic for a frame set. `auto` may fall back to software
for the complete transaction, but M13 never mixes software and GLES outputs in
one commit. Per-output diagnostics record upload and readback bytes, damage,
filtering, scale, transform, fallback reason, and maximum fractional comparison
error.

## Frame identity

Each output keeps its historical visible-pixel hash. M13 additionally computes
an aggregate FNV-1a hash over a fixed frame-set tag, layout generation, primary
output, and every enabled output's ID, physical extent, scale, transform, and
visible hash in stable ID order. The aggregate is evidence metadata; it never
replaces historical one-output fixtures.

Headless presentation publishes every staged output artifact atomically. The
DRM presenter accepts exactly one native-sized output frame and rejects a
multi-output set before any KMS mutation.
