# Rust Toolchain Policy

Status: WP0C accepted policy
Date: 2026-08-20

Glasswyrm Rust crates use edition 2024 and declare an MSRV of Rust 1.95.
`rust-toolchain.toml` is tracked and pins `1.95.0` with the minimal profile,
Clippy, and rustfmt. That tracked file is the reproducible project toolchain
identity; do not replace it with an unrecorded host-default compiler.

The Gentoo development host used for WP0C currently provides system
`rustc 1.96.1` without rustup. It satisfies the declared MSRV and is acceptable
for ordinary development and compatibility validation, but a successful build
with 1.96.1 is not evidence that the exact 1.95.0 pin was executed. Acceptance
records must state both the tracked pin and the active compiler.

Cargo transition/package checks use `--locked --offline`. At the WP0C smoke
revision, `Cargo.lock` contains no external `source =` entries, so the current
workspace resolves entirely through tracked/path dependencies and requires no
network or registry fetch during the smoke. If a registry, git, build-script,
or proc-macro dependency is added later, the package policy must explicitly
pin and vendor/cache it before the offline gate can remain accepted.

Gentoo packaging may use the distribution Rust toolchain when it satisfies the
MSRV, but it must preserve the tracked toolchain identity in provenance and
must not silently lower the minimum or advance language assumptions beyond the
pin. Final ebuild dependency/version policy is a WP7 packaging decision.
