# Rust Transition WP0C Status

Status: topology lane complete; toolchain/install and public-C-ABI lanes in progress
Base implementation commit: `eb592dec5085f55a94d47f0a62257d2dc3d684ee`
Date: 2026-08-20

## Topology matrix

WP0C now classifies all eight legacy/Rust combinations in
`RUST_TRANSITION_TEST_MAP.md`. The four combinations with the retained legacy
server are runnable. The four Rust-server combinations are explicitly blocked
at the current implementation level because Rust `glasswyrmd` has only X11
`--display`/`--socket-dir` process options and no WM/compositor GWIPC
peer/bootstrap integration yet. That gap belongs to WP5 rather than being
papered over with a weaker test.

The previously missing runnable edge is now exposed as:

```sh
cargo xtask --legacy-build <meson-build> test mixed peers
```

It runs the existing M14 output-restart scenario with legacy `glasswyrmd`, Rust
`gwm`, and Rust headless `gwcomp`. The harness keeps the legacy server/control
oracle, accepts explicit peer executable overrides, preserves structured
failure bundles, and records the selected topology in its scenario trace.

## Verified combined-peer result

A project-local Meson oracle was configured under
`artifacts/rust-transition/wp0c/legacy-build` with DRM, libinput, and GL paths
disabled and `glasswyrmd` built successfully. The combined Rust-peer profile
then passed with:

- initial generation-1, 1280x480, two-output M14 snapshot;
- queued vertical reconfiguration while the server was stopped;
- compositor replacement and first-commit rejection retaining generation 1;
- second commit accepted as generation 2 at 640x960; and
- both Rust peers operating simultaneously behind the unchanged legacy server.

The harness runtime slug is intentionally compact so both `gwm.sock` and
`gwcomp.sock` remain below Linux `sun_path` limits while temporary state stays
inside the OS-Project write boundary.

## Remaining WP0C work

- prove the reproducible Rust toolchain/offline package path and empty-DESTDIR
  install smoke for a Rust executable plus the bounded Rust C-ABI bridge;
- detect ownership collisions against the retained Meson install surface; and
- inventory actual GWIPC 0.9 external consumers and record the public ABI
  preservation/deprecation disposition.
