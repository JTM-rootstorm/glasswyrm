# Headless Frame Dumps

The frame-dump primitive writes one binary Netpbm PPM (`P6`) file per accepted
frame. Process integration with accepted frame commits is not implemented yet;
the format below describes and tests the reusable dump component.

## Bytes and naming

The file header is ASCII with no comments:

```text
P6\nWIDTH HEIGHT\n255\n
```

It is followed immediately by exactly `width * height * 3` bytes in row-major
order, top to bottom and left to right. Each pixel is written as red, green,
blue; the X byte from the XRGB framebuffer is omitted.

Names use decimal, zero-padded fields:

```text
frame-NNNNNN-output-IIIIIIIIIIIIIIII.ppm
```

The frame field has minimum width 6 and the output identifier minimum width 16;
values wider than those fields are not truncated. A temporary file in the dump
directory is written, flushed with `fsync`, closed, then atomically renamed to
the final name.

## Manifest and hash

`frames.jsonl` receives one compact JSON object per dump with these fields in
order:

- `frame`: compositor frame sequence;
- `commit_id`: correlated producer commit identifier;
- `generation`: presented producer generation;
- `output_id`: output identifier;
- `width` and `height`: framebuffer dimensions;
- `damage_rectangles`: number of output damage rectangles;
- `fnv1a64`: 16-digit lowercase hexadecimal FNV-1a hash;
- `file`: PPM basename.

The hash input is only the raw RGB payload, not the PPM header or manifest. It
uses the 64-bit FNV-1a offset basis `14695981039346656037` and prime
`1099511628211`, processing bytes in file payload order.

## Golden regeneration

Golden artifacts should be regenerated only after an intentional renderer or
format change:

1. Run the focused renderer and headless-output tests first.
2. Generate frames with the repository-owned deterministic scenario once that
   producer is implemented.
3. Inspect PPM dimensions, manifest metadata, and a byte-level or pixel-level
   diff rather than accepting new hashes alone.
4. Replace the expected PPM and hash together and record the semantic rendering
   change in the commit.
5. Re-run the complete test suite under a clean build.

At present, `headless-output` contains the authoritative small byte/hash test;
there is no end-to-end producer golden-regeneration command yet.
