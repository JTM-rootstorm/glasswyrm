# Rust Transition WP1 Completion Status

Date: 2026-08-30
Plan authority: `../../../GLASSWYRM_RUST_FIRST_AND_WYRROOT_PORTING_PLAN.md`, Section 7
Status: complete within the WP1 software-only boundary

## Exact candidate

- Branch: `migration/rust-first`
- Starting revision: `d0534ffba5d27cdfdc7038fd8b9f6e098fb8fb5b`
- WP1 implementation revision: `add1b79bca24ce8c97b37e106e942d8b1abf5efa`
- Implementation tree: `4d5253d87153ae5ea397dcc911ff90a7150f217e`
- Starting-revision-to-implementation diff SHA-256:
  `7c7bea85e5f741cec6cffc76d019bc4ea88f7f05cba093b945d3dc2180329e50`
- Commit signature status: unsigned (`%G? = N`)

The tracked implementation tree was clean after the implementation commit.
Pre-existing untracked operator/planning material (`.codex/`, `Plans/`, `m14`,
and `m14-export`) was preserved and was not treated as source or evidence.

## Completed WP1 surface

- Setup, both X11 byte orders, ordinary and BIG-REQUESTS framing, replies,
  events, errors, and extension discovery retain their frozen wire contracts.
- The canonical core-request table now covers all 79 retained core opcodes.
  The generated inventory verifies an actual Rust decoder function for 79/79
  entries and records the exact current 14/79 stateful dispatch subset.
- The final 13 drawable, GC, and raster request shapes are decoded with
  checked sizes and borrowed/lazy variable payloads. Validation that must
  follow resource lookup remains an explicit post-resource step so legacy
  error precedence is preserved.
- The nine retained extension descriptors remain metadata-compatible with the
  legacy registry. WP1 does not enable new extensions or add extension
  behavior beyond the existing BIG-REQUESTS path.
- `ClientSession` remains byte-driven and independent of listener paths,
  credentials, nonblocking flags, and shutdown syscalls. Direct pending input
  is capped before copying, output remains bounded, and one atomic request is
  bounded by the advertised 16 MiB BIG-REQUESTS maximum.
- Signal cancellation now reaches setup and established client workers.
  Workers are retained, reaped, and joined, and cancellation performs session
  cleanup and releases the client's resource-ID lease.
- Lifecycle decoders accept ordinary and normalized extended request headers.
- Inventory schema v3 replaces the impossible commit self-reference with a
  deterministic declared-input closure. A clean post-commit check succeeds.

The checked-in inventory at the implementation revision has:

- inventory SHA-256:
  `6f32953af3eccf85ab197d56cc25990e2ab597e2e397db7e6175fcfe05d6e91e`;
- declared-input closure: 556 files;
- closure SHA-256:
  `7bdc94fa9c8cc08a2a7c77a6aab42d252873ade92ab5c396cc5338f354f00c73`;
- core decoder functions: 79/79;
- core dispatch entries: 14/79; and
- extension descriptors: 9.

## Required sources and provenance

The implementation and validation used these required sources:

- root WP1 plan, SHA-256
  `992952f8dbb912a2b666d9e895dbe307515f5769b506e0a834f81238b30c0c1c`;
- `docs/GLASSWYRM_SPEC.md`, SHA-256
  `ea4453a39d8c95e44343462c992b3727da242cda2fc84d250acadcf59451db33`;
- `docs/decisions/0017-rust-first-transition.md`, SHA-256
  `9791e5d313aa8b912ce2d7bb677bcc1d2965b380efaf862589fe3d98daf8fd85`;
- `docs/decisions/0018-rust-server-execution-model.md`, SHA-256
  `1aaf78a15ffdc13d8f0483e378313463c4f195c9b0e202db197442fcfcee33a4`;
- retained request dispatcher and drawable/raster handlers at starting revision
  `d0534ffba5d27cdfdc7038fd8b9f6e098fb8fb5b`; and
- installed `xcb-proto` 1.17.0 `/usr/share/xcb/xproto.xml`, SHA-256
  `bb8c060a8ea009a5d08a57c0c6daa1d3dd7e9d743d46d055109505c344a1f4c9`.

The XCB protocol description informed wire field layout only. No third-party
code was copied. New Rust code remains under the repository's existing license
boundary.

## Validation

All commands were run from the Glasswyrm repository. Linux socket/credential
tests were run outside the managed sandbox because the sandbox rejects the
required socket operations with `EPERM`.

- `cargo fmt --all -- --check`: passed.
- `cargo clippy -p glasswyrm-x11 -p glasswyrmd --all-targets --locked --offline -- -D warnings`:
  passed.
- `cargo test -p glasswyrm-x11 --locked --offline`: passed (62 unit tests and
  17 integration/oracle tests).
- `cargo test -p glasswyrmd --locked --offline`: passed (35 unit tests and 9
  process tests).
- `cargo xtask --legacy-build artifacts/rust-transition/wp1/legacy-build test unit`:
  passed, including all Rust workspace library tests and 137/137 retained
  Meson Tier-1 tests.
- `cargo xtask --legacy-build artifacts/rust-transition/wp1/legacy-build test contract`:
  passed, including all Rust workspace integration tests and 91/91 retained
  Meson Tier-2 tests.
- Retained native `glasswyrmd-handshake`, `glasswyrmd-malformed-client`, and
  `glasswyrmd-core-requests`: passed 3/3.
- Rust process scenarios run against the freshly built retained native server:
  property 2/2, window 2/2, and extension discovery 1/1 passed.
- `sha256sum --check tests/fixtures/x11-transition/SHA256SUMS`: passed for all
  14 frozen files.
- `python3 scripts/generate-rust-transition-inventory.py --check`: passed both
  before and after the implementation commit.
- `git diff --check`: passed.

The retained native build used Meson 1.11.2, GCC/G++ 15.3.0, XCB 1.17.0,
`werror=true`, `drm_backend=false`, `libinput_backend=false`, and
`render_gl=false`. It was a local software oracle build, not an acceptance
artifact.

## Adversarial review

A WP1-scoped review used exact model `gpt-daybreak-blue-latest`, reasoning
effort `high`, on 2026-08-30 against starting revision
`d0534ffba5d27cdfdc7038fd8b9f6e098fb8fb5b` and final diff SHA-256
`7c7bea85e5f741cec6cffc76d019bc4ea88f7f05cba093b945d3dc2180329e50`.

The initial review found two Medium issues (worker cancellation did not reach
established sessions; direct session input was copied before a bound) and one
Low issue (lifecycle decoders rejected normalized BIG headers). All three were
fixed. The targeted final review found no Critical, High, Medium, or Low
findings and no blocking issue. This is a WP1 adversarial review, not the final
WP6/WP7 security gate.

## Nonclaims

WP1 does not claim stateful runtime support for the 65 decoded-but-not-yet-
dispatched core requests. Those resource, lifecycle, routing, input, rendering,
and integration semantics remain owned by WP2-WP5. It does not claim new
extension behavior, legacy production deletion, all-Rust software/package
acceptance, VM acceptance, physical-hardware acceptance, the WP6 unsafe/native
boundary gate, the WP7 retirement gate, or Wyrmroot portability.
