# Rust Transition WP0C Status

Status: WP0C complete
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

## Toolchain and install smoke

`RUST_TOOLCHAIN_POLICY.md` records edition 2024, MSRV 1.95, and the tracked
1.95.0 toolchain pin. The Gentoo host smoke used rustc 1.96.1, explicitly
recorded as MSRV-compatible development validation rather than execution of the
exact pin. Cargo built the Rust candidates with `--locked --offline`; the lock
file contained no external source entries.

`scripts/wp0c-install-smoke.py` staged the complete retained Meson install into
an empty project-local DESTDIR using prefix `/usr` and libdir `lib64`, then
installed a Rust `gwm` candidate plus the bounded Rust C ABI artifacts under
transition-specific names. The manifest hashes each Rust artifact and records
the planned `/usr/bin/gwm`, public-header, `libgwipc.so.0`, and `gwipc.pc`
ownership collisions without overwriting the retained install.

## Public C ABI disposition

`GWIPC_RUST_TRANSITION_ABI_DISPOSITION.md` selects full preservation of the
installed GWIPC 0.9 ABI. No consumers were found in the other OS-Project
repositories, while the repository-owned staged consumer matrix covers C17 and
C++20 clients for every API generation 0.1 through 0.9. All of those unchanged
consumers passed against the staged native install. The bounded Rust bridge
remains non-public until it covers the complete installed contract.

## WP0C gate

All eight cutover edges are classified, the missing dual-Rust-peer topology is
green behind the legacy server, the locked/offline empty-DESTDIR package smoke
is green, and the installed C ABI has an explicit ownership/disposition record.
WP0C is therefore complete. Rust-server topology rows remain intentionally
blocked until WP5 implements the peer/bootstrap surface they require.
