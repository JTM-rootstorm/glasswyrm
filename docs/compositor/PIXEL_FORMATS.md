# Milestone 4 Pixel Formats

Buffers contain native little-endian 32-bit words. In numeric bit notation both
formats use:

```text
31          24 23          16 15           8 7            0
+--------------+--------------+--------------+--------------+
| alpha or X   | red          | green        | blue         |
+--------------+--------------+--------------+--------------+
```

On the targeted little-endian x86_64 host, bytes in increasing address order
are blue, green, red, then alpha/X. Rows begin at `row * stride`; pixels within
a row are four bytes apart. A view is rejected if dimensions are zero, stride
is smaller than `width * 4`, or its byte span cannot cover the final row.

## XRGB8888

Bits 23:16, 15:8, and 7:0 are red, green, and blue. Bits 31:24 are ignored on
input and the source is treated as alpha 255. The headless output stores these
bits as `0xff` for deterministic XRGB pixels.

## Premultiplied ARGB8888

Bits 31:24 contain alpha. Red, green, and blue must already be multiplied by
alpha, so every channel must be less than or equal to alpha. The renderer
validates every sampled pixel before writing any destination pixel and rejects
the operation if this invariant is violated.

## Global opacity and blending

Global opacity is unsigned 16.16 fixed point: `0` is transparent and `0x10000`
is fully opaque. Larger inputs are clamped to full opacity. Each premultiplied
source channel, including alpha, is scaled as:

```text
scaled = (channel * min(opacity, 0x10000) + 32768) >> 16
```

The scaled premultiplied source is composed over an opaque XRGB destination:

```text
out = min(255, source + (destination * (255 - source_alpha) + 127) / 255)
```

All operations use integer arithmetic. The `32768` and `127` terms specify
round-to-nearest behavior at the respective fixed-point divisions. Output
alpha is always 255. No gamma conversion or color management occurs in M4.
