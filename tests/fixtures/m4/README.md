# Milestone 4 Golden Frames

This directory contains the reviewed, deterministic output of the Milestone 4
synthetic-producer scenarios. The canonical headless output is 320 by 200 sRGB
pixels. PPM files use the byte format and naming rules documented in
`docs/compositor/FRAME_DUMPS.md`.

`SHA256SUMS` is consumed by the Gentoo VM acceptance harness from inside the
runtime frame directory. Its paths therefore name the generated PPM files and
`frames.jsonl` directly, without a repository-relative prefix.

Golden files must cover the canonical scene and its required mutations:

- the initial XRGB and premultiplied-ARGB composition;
- a bounded damaged-buffer update;
- restacking;
- visibility changes;
- clipping, including partially offscreen geometry;
- zero, half, and full global opacity;
- a reconnect snapshot that reproduces an earlier payload hash.

Do not update these files as a side effect of an ordinary test. To regenerate
them intentionally, build `gwcomp` and `gwcomp_m4_producer`, run the complete
scenario sequence documented in `docs/compositor/FRAME_DUMPS.md`, inspect the
PPM payloads and manifest diff, and then regenerate `SHA256SUMS`. A hash change
must be reviewed together with the corresponding pixel and semantic change.

The manifest FNV-1a value covers only the raw RGB payload. `SHA256SUMS` covers
the complete PPM files, including their headers, and the complete manifest.
