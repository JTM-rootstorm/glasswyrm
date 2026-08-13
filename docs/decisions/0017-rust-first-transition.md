# 0017: Make the Glasswyrm Runtime Rust-First

Status: Accepted for the Rust transition

## Context

Glasswyrm reached the end of Milestone 14 with explicit process and wire
boundaries, deterministic headless coverage, and guarded physical validation.
Those boundaries make it possible to replace one implementation at a time.
The remaining constraint is the C++-heavy edit/build/test loop: small policy or
harness corrections can currently lead to rebuilding and rerunning much more
of the stack than is capable of disproving the change.

The immutable behavior/evidence anchor for the transition is
`36009a8bbe50794d6808d142ac57b623062a1e63`. The transition branch carries
forward source `a39c7788bb5226e793698192028b9a4a57202f8d`, which descends from
that anchor by eleven software, diagnostic, provenance, and guarded-harness
commits. The latter is the implementation starting point; it does not silently
supersede the former as accepted behavior or physical evidence.

The R0 software baseline is recorded at
`artifacts/rust-transition/baseline/manifest.json`: legacy configure/build and
the full 286/286 Meson software suite passed, as did focused headless (5/5),
restart (4/4), and harness-selftest (5/5) groups. No hardware validation was
run or inferred from this record.

## Decision

Glasswyrm becomes Rust-first without changing its architecture:

- `glasswyrmd` remains protocol truth;
- `gwm` remains window-management policy truth;
- `gwcomp` remains final composition and display authority; and
- GWIPC, X11-visible behavior, M13 scaling semantics, and M14 VRR semantics
  remain compatibility boundaries.

Rust is the default for new runtime code and for migrated state, policy,
protocol, IPC, orchestration, and tool code. C is retained only for bounded
Linux/platform or ABI shims where it is simpler and more auditable. C++ is
migration-only legacy code or temporary comparison glue. Assembly remains an
optional, benchmark-proven optimization with a correct Rust or C reference
path.

Cargo is the primary Rust inner loop. Meson/Ninja continues to build the
legacy C/C++ tree and retained native shims during migration. The two build
graphs remain independently usable: targeted Cargo checks must not trigger the
whole Meson build, and ordinary Meson configuration must not build all Rust
tests. A deliberate orchestration command may combine them for checkpoints.
Meson retirement is a later, separately reviewable decision after production
C++ is gone.

Validation is split into explicit tiers:

1. compile/type feedback;
2. unit and pure-policy tests;
3. wire, contract, component, fixture, and cross-language tests;
4. minimal-process and headless integration;
5. full software, compatibility, sanitizer, install, and applicable Gentoo VM
   acceptance; and
6. explicitly authorized physical hardware acceptance.

The cheapest capable tier runs first. A migrated test must first reproduce the
legacy result, then exercise the Rust replacement with the same accepted
fixture. Legacy and Rust implementations are not permanent peers; the legacy
implementation is removed after the replacement's compatibility gate passes.

Physical hardware is suspended as an edit/test loop. Hardware-capable commands
must fail closed unless `GW_ALLOW_HARDWARE_TESTS=1` is set to the exact value
`1`, ordinary test wrappers must omit that variable, and physical M14
re-acceptance occurs only after all software tiers are green and Mike
explicitly authorizes it. Fake DRM, recorded state, headless simulation, and
QXL can prove policy, transactions, negative capability, and harness
interpretation, but never positive physical VRR behavior.

Milestone 15 color-management and HDR feature work is frozen until the
Rust-first transition and bounded M14 re-acceptance gate pass. Critical
correctness fixes may proceed, but new M15 architecture must not accumulate in
the C++ tree.

## Consequences

- Migration can proceed process by process rather than as a flag-day rewrite.
- Canonical fixtures, message IDs, widths, byte order, FD ownership, ordering,
  snapshot/replay, and tool behavior remain stable migration oracles.
- Unsafe and native code is quarantined behind narrow platform/FFI crates and
  documented safety invariants.
- The current 286-test Meson suite must gain explicit classifications and
  smaller entry points instead of remaining one undifferentiated acceptance
  surface.
- Harness defects receive focused regression tests and do not force physical
  reruns.
- Cargo dependencies and crate boundaries require the same scrutiny as native
  dependencies; Rust-first does not justify a framework-heavy rewrite.
- M15 schedule work waits while the implementation foundation changes.

## Revisit when

Revisit the final build arrangement after production C++ has been retired and
the remaining C/assembly surface is known. Revisit an individual native
boundary only with measured complexity, correctness, or performance evidence.
Do not revisit the three-process authority split as part of the language
transition.
