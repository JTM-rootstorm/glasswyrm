# Milestone 13 scale and transform rendering

Milestone 13 renders in global logical desktop coordinates and produces one
native physical frame per enabled output. Output scale is a reduced rational
number of physical pixels per logical unit. Client buffer scale is separate:
legacy clients publish scale-1 buffers, while the repository `GW_SCALE` client
may publish integer-scale buffers from 1 through 4 without changing logical
window geometry.

For a surface on an output, the effective sampling ratio is:

```text
output scale / client buffer scale
```

## Sampling contract

- Exact 1:1 mapping uses direct sampling.
- Exact integer upscaling uses nearest-neighbor replication.
- Fractional scaling and downscaling use fixed bilinear filtering.
- ARGB interpolation operates on premultiplied channels.
- The software reference uses checked fixed-point weights with round-half-up;
  it does not use floating point.

Software is canonical. Direct and integer-nearest GLES fixtures must be
byte-identical to it. Fractional GLES comparison permits at most one value of
error per color channel when exact shader quantization is unavailable.

Bilinear filtering expands mapped native-output damage by one pixel on each
relevant edge, clipped to output bounds. Direct and nearest sampling do not add
filter padding. Scaled client-pixmap damage remains in buffer pixels until the
server floors lower logical bounds and ceilings upper logical bounds by the
integer client scale.

## Transforms

The eight output transforms are applied after logical placement and scale:

- Normal;
- Rotate90, Rotate180, and Rotate270 clockwise;
- Flipped, a horizontal reflection;
- Flipped90, Flipped180, and Flipped270.

Quarter-turn transforms swap the physical extent before logical-size
derivation. Mapping uses half-open rectangles and deterministic edge rules.
The renderer performs inverse output-to-logical sampling, so X11 coordinates
do not rotate. A physical dump and DRM frame are always in native scanout
orientation.

M13 does not use KMS rotation properties, hardware cursors, configurable
high-quality filters, or surface-local transforms.
