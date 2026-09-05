# Rust Toolchain Policy

Status: active policy; prior 1.97.1 repin superseded by the 1.98.1 repin
Date: 2026-09-05

Glasswyrm Rust crates use edition 2024 and declare an exact minimum of Rust
1.98.1. `rust-toolchain.toml` is tracked and pins `1.98.1` with the minimal
profile, Clippy, and rustfmt. That tracked file is the portable project
toolchain identity; do not replace it with an unrecorded host-default compiler.

The canonical host toolchain for this pin is the installed Gentoo
`rust-bin-1.98.1` tree at `/opt/rust-bin-1.98.1`. Exact-pin validation invokes
its `cargo`, `rustc`, `rustfmt`, and `clippy-driver` binaries and records their
reported identities. A successful build with another host-default or compatible
compiler is not evidence that the exact 1.98.1 pin was executed.

The original WP0C smoke used system `rustc 1.96.1` while the tracked project
pin was 1.95.0. The later 1.97.1 repin and its validation likewise remain
historical evidence for their exact revisions. Neither establishes the current
1.98.1 toolchain gate.

Cargo transition/package checks use `--locked --offline`. At the WP0C smoke
revision, `Cargo.lock` contains no external `source =` entries, so the current
workspace resolves entirely through tracked/path dependencies and requires no
network or registry fetch during the smoke. If a registry, git, build-script,
or proc-macro dependency is added later, the package policy must explicitly
pin and vendor/cache it before the offline gate can remain accepted.

Gentoo packaging may use the distribution Rust toolchain only when it satisfies
the 1.98.1 minimum, but it must preserve the tracked toolchain identity in
provenance and must not silently lower the minimum or advance language
assumptions beyond the pin. Final ebuild dependency/version policy is a WP7
packaging decision.
