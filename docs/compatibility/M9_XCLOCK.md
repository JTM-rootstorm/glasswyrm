# Milestone 9 xclock profiles

Status: accepted for the pinned commands below.

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
`clock_gettime` only; monotonic time passes through. The Gentoo VM builds the
verified source and proves exact analog and digital frames, normalized traces,
combined-client isolation, restart behavior, and clean process/socket teardown.
