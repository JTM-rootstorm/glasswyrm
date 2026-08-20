# Rust Transition WP0 / WP0A / WP0B Status

Status: WP0 / WP0A / WP0B implementation complete; WP0C not started
Reconciled worktree base: `ae96f0f13c050cf1751ab94047925e896308b262`
Reconciliation date: 2026-08-20

## Scope

This record closes the requested WP0 core inventory, WP0A resumable validation,
and WP0B server execution-model/portability-seam implementation. WP0C mixed
three-process topology expansion, Gentoo/install smoke, and public C ABI
disposition are intentionally not part of this closure.

The worktree began at the active plan anchor with no later commits. Existing
untracked `.codex/`, `Plans/`, `m14`, `m14-export`, and Python cache material
was preserved. No VM or physical-hardware operation was performed.

## Reconciled baseline corrections

Two statements in the 2026-08-17 plan audit were stale against the tracked tree:

- `rust-toolchain.toml` is tracked and pins Rust `1.95.0`, minimal profile,
  `clippy`, and `rustfmt`;
- `gw-platform-linux` already protects shared mappings against concurrent
  truncate/SIGBUS by requiring shrink and grow seals before `mmap`, with
  negative tests for unsealed objects, invalid ranges, and post-map truncate.

## WP0 core inventory

`scripts/generate-rust-transition-inventory.py` derives
`docs/maintenance/RUST_TRANSITION_WP0_INVENTORY.json` from tracked source.
`--check` fails on drift.

Inventory schema v2 records:

- 79 enumerated core X11 opcodes, including 14 currently represented in Rust
  dispatch;
- 9 legacy extension descriptors;
- 195 tracked legacy `glasswyrmd` C++ source/header files;
- 97 tracked server-relevant tests;
- all 217 tracked fixture files, individually SHA-256 bound and also covered by
  a whole-tree hash;
- exact legacy/Rust implementation mapping, fixture families, mixed-stack
  profiles, and cutover phase for every core request and extension;
- WP1-WP5 ownership plus retention/deletion gates for every legacy server source
  and relevant test;
- five migration/compatibility adapters and three named compatibility constants;
- Linux shared-buffer, endpoint ancestry/same-EUID, role/capability, and active
  peer-cardinality boundary records.

Seven frozen contract families cover X11 wire bytes, GWIPC bytes, structural
event ordering, input event ordering, rendering/scene snapshots, M13
scale/output behavior, and M14 VRR behavior. M13/M14 vocabulary documents are
also individually hashed.

The legacy server-owned extension XID range is no longer an unexplained magic
literal: `kServerExtensionResourceFirst` and
`kServerExtensionResourceLast` name `0x1FFFF100..0x1FFFF1FF`, with compile-time
boundary regression coverage in `tests/unit/resource_id_test.cpp`. That test
also compiles and runs directly under C++23 with
`-Wall -Wextra -Wpedantic -Werror`.

Preservation targets verified present include `.codex/`, `m14`, `m14-export`,
`artifacts/rust-transition/`, the historical VM result, fifteen
`Plans/m14-vrr-hardware-*.toml` files, both history-rewrite backup refs, and both
workspace backup-bundle hashes recorded by the root plan.

## WP0A resumable validation

`cargo xtask campaign` now exposes a named 21-step graph with `--list`,
`--only`, `--from`, `--resume`, and `--rerun-failed`. Step fingerprints bind the
command, tool executable, declared source/configuration/fixture paths, and
upstream fingerprints. Independent steps continue after failures while
unsatisfied dependents are blocked.

Development manifest/cache format v2 binds revision/dirty state, commands,
inputs, results, evidence-run identity, stdout/stderr SHA-256s, and declared
build outputs/hashes. A cached successful step is reusable only when:

- its input fingerprint still matches;
- required build outputs still exist; and
- its retained stdout/stderr evidence still exists and matches the stored hashes.

A dedicated unit regression mutates retained stdout and proves the cache becomes
invalid. Failure bundles are path-relative and self-contained with command,
status, timeout, input manifest/hash, stdout/stderr, and their hashes. Campaign
commands remove hardware authorization, force Cargo offline, enforce bounded
timeouts, and use the short project-local `OS-Project/.t` temp root.

The final injected late-failure proof passed: `transition-inventory-check`
succeeded, `hardware-harness-selftest` was deliberately failed, and `--resume`
reused only the untampered inventory evidence while rerunning the failed step.
The resumed manifest points back to the original evidence run and exact log
hashes.

`cargo xtask replay` provides parser-only M14 NVIDIA-probe replay plus complete
M14 fixture/dry-run replay without entering the live hardware path. Replay-v2
manifests hash generated output and captured stdout/stderr. The NVIDIA parser
path passed against the repository's canonical synthetic probe generator. No
preserved physical NVIDIA probe report exists in this checkout, so this record
makes no physical replay or hardware-acceptance claim.

## WP0B execution model and portability seams

ADR 0018 accepts serialized canonical server mutation plus recipient snapshots
that reserve client identity, X11 byte order, recipient sequence, and queue
generation before lock release. Queue/socket work happens afterward. Saturation
or disconnect affects only that recipient and retained bytes are cleared
deterministically.

`ClientSession` now owns byte-driven X11 setup and established processing.
Linux listener/credential/deadline/polling/shutdown mechanisms remain outside the
session, and `RequestLoop` writes through `Write` rather than `UnixStream`.

GWIPC handshake validation consumes transport-neutral `RecordSource` values and
semantic attachment counts. `HANDLE_ATTACHMENTS` and `SHARED_BUFFER_OBJECTS`
are the portable capability names; Linux-v1 `FD_PASSING` and `MEMFD_BUFFERS`
remain exact-value compatibility aliases. Existing pure `gwm-core` and
`gwcomp-core` reducers remain separate from Linux process/transport adapters.

The socket/device-free WP0B gate passed independently:

- execution-model prototype: 3 tests;
- byte-driven client session: 2 tests;
- in-memory GWIPC handshake: 6 tests;
- semantic GWIPC record: 1 test;
- `gw-types`: 4 tests.

The Linux adapter preservation set then passed in full: `glasswyrmd` 27 unit
plus 6 current process-oracle tests, `gw-ipc` 39 tests, `gw-platform-linux` 9,
`gwcomp` 23, and `gwm` 6. The xtask suite is 16/16. Strict Clippy with
`-D warnings`, Rust formatting, Python compile, deterministic inventory
`--check`, and `git diff --check` pass.

## Global review qualification

WP0/WP0A/WP0B do not themselves carry the later exact-revision Daybreak phase
gate; the native-boundary/production review is enforced in WP6/WP7. Section 16
of the root plan nevertheless says the rewritten plan should have received a
fresh `gpt-daybreak-blue-latest` review before implementation began. That lane
was not available through this Desktop Commander session, so this is recorded
as a global process exception rather than invented evidence. No final security
or release acceptance claim is made here.
