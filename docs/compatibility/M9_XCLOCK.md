# Milestone 9 xclock profiles

Status: protocol foundation implemented; external acceptance pending.

Supported targets:

- `xclock` 1.2.0 analog with `-norender -update 0`;
- `xclock` 1.2.0 digital with `-brief -twentyfour -norender -update 0`;
- fixed test time `2026-01-02 03:04:05 UTC`;
- C locale and the built-in fixed font.

Unsupported:

- Render/Xft paths;
- locale font sets;
- chime/XKB behavior;
- arbitrary fonts or another xclock release without a new audit.

The fixed-time test preload changes `time`, `gettimeofday`, and realtime
`clock_gettime` only; monotonic time passes through. The analog raster, core
text, child composition, trace, and harness foundations exist. Exact analog,
digital, combined, restart, source-hash, trace, and frame evidence remain
pending in the Gentoo acceptance VM.
