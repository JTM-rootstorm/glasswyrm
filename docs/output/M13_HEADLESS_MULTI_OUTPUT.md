# Milestone 13 headless multi-output

The headless backend supports one through eight stable logical outputs in the
opt-in output-model profile:

```sh
gwcomp --backend headless \
  --ipc-socket /run/glasswyrm/gwcomp.sock \
  --dump-dir /var/tmp/glasswyrm-frames \
  --headless-output LEFT:640x480@60000 \
  --headless-output RIGHT:800x600@60000
```

The specification is `NAME[:WIDTHxHEIGHT[@MILLIHZ]]`. Names must be unique
bounded ASCII identifiers. The first named output is initially primary; named
outputs begin enabled, arranged left to right at scale `1/1` and Normal
transform. Their descriptor identities remain stable across GWM and compositor
restart.

When no `--headless-output` is supplied and output-model capabilities are not
negotiated, `gwcomp` creates the historical `HEADLESS-1` 1024x768 output and
preserves the Milestone 4-12 filenames, hashes, `frames.jsonl`, and directory
layout byte-for-byte.

## Atomic output artifacts

An M13 frame transaction renders every enabled output before presentation.
Files are staged before any is published. A failed write aborts every temporary
artifact and leaves the previously committed scene and layout visible.

M13 preserves the existing flat, decimal-padded PPM filename serialization:

```text
frame-000001-output-<decimal-padded-output-id>.ppm
frames.jsonl
frame-sets.jsonl
```

`frames.jsonl` retains its historical per-frame bytes. Each physical PPM uses
native output orientation and XRGB8888-derived pixels. `frame-sets.jsonl`
records transaction, scene and layout generations, primary output, output
count, per-output recorded filename and visible hash, physical and logical
geometry, scale, transform, damage, and the aggregate frame-set hash. Its
identity fields use fixed-width hexadecimal text independently of the preserved
decimal filename. Disabled outputs produce no PPM but remain visible through
output-control diagnostics.

The canonical M13 acceptance layout uses a `640x480` LEFT output at `1/1` and
an `800x600` RIGHT output at `5/4`. Both have logical extent `640x480`; RIGHT
is positioned at `(640,0)`, producing a `1280x480` root. Software output is
canonical. GLES comparisons retain software fixtures and the documented
fractional error bound.

Several physical DRM connectors, hotplug, and persistent output placement are
not part of the headless proof.
