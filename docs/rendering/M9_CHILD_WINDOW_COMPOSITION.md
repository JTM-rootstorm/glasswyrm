# Milestone 9 child-window composition

Milestone 9 keeps X11 child windows inside `glasswyrmd` and publishes one
opaque buffer per managed top-level window. This preserves the established
authority boundary: the server owns window protocol semantics and `gwcomp`
owns final display composition.

## Composition rules

The direct-root InputOutput window supplies the output dimensions and base
pixels. Its viewable InputOutput descendants are walked recursively in X11
child stacking order. Child origins include their configured position and
border width. Every descendant is clipped by its parent clip and the top-level
bounds. An unbacked window contributes its deterministic background; backed
windows contribute opaque RGB pixels. InputOnly, unviewable, missing, and
out-of-bounds children do not paint.

The result is a fresh, bounded `PixelStorage`. Failure to identify a valid
direct-root InputOutput top-level or allocate the result produces no composed
buffer. The implementation does not expose child surfaces through GWIPC.

## Publication and limits

The existing `ContentPresenter` remains the publication boundary. Child draw
and lifecycle damage is translated to the top-level presentation domain, and
republication uses the same buffer/release/replay contract as Milestone 7.
GWIPC API 0.5, SOVERSION 0, and wire version 1.0 do not change.

This reference path favors deterministic correctness over incremental-copy
optimization. Borders are positioning/clipping inputs, not separately styled
decorations. Shape masks, alpha child surfaces, redirected child pixmaps, and
independent compositor policy for children are outside M9.
