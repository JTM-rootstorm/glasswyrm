# Rust Transition Test Map

Status: R0 classification at carried-forward source
`a39c7788bb5226e793698192028b9a4a57202f8d`

Behavior/evidence anchor:
`36009a8bbe50794d6808d142ac57b623062a1e63`

## Current baseline

The configured full-feature legacy build exposes 286 Meson tests. The Meson
manifests classify ordinary tests into `tier1-unit`, `tier2-contract`, or
`tier3-headless-process`; every software test also belongs to the aggregate
`tier4-software` suite. Offline hardware harness selftests are additionally
labeled `hardware-selftest`, while the existing M14 runtime labels remain.
File location alone is not reliable: `tests/integration/` contains both
component contracts and multi-process scenarios, while `tests/hardware/m14/`
contains offline unit tests for the hardware harness rather than physical
acceptance itself.

The captured R0 software record is
`artifacts/rust-transition/baseline/manifest.json`. It records a clean legacy
configure/build, 286/286 Meson software tests, focused headless 5/5, restart
4/4, and harness selftests 5/5. It contains no hardware acceptance claim.

The migration must preserve the accepted fixture bytes, normalized traces,
frames, hashes, process ordering, and M13/M14 evidence vocabularies. Do not
regenerate a fixture merely because the Rust representation differs.

## Target tiers

| Tier | Purpose | Ordinary trigger | Must not do |
|---|---|---|---|
| 0 | Compile, type, format, and lint feedback for the touched crate/target | Small edits | Spawn runtime processes, consume fixtures, or access hardware |
| 1 | Unit and deterministic pure-policy/backend tests | Continuous implementation loop | Require sockets shared with another process or a real device |
| 2 | Wire, ABI, contract, component, malformed-input, and fixture tests | Subsystem checkpoint | Launch the complete stack when one peer/codec suffices |
| 3 | Minimal-process and headless integration | Before a process-behavior commit | Acquire DRM master or depend on physical output behavior |
| 4 | Full software acceptance, compatibility, sanitizers, install/package checks | Phase gate | Claim positive physical VRR/HDR/display acceptance |
| 4-VM | Applicable Gentoo/QXL acceptance in the `glasswyrm` VM | After local Tier 4 is green | Treat virtual/QXL negative capability as physical proof |
| 5 | Bounded physical DRM/KMS and VRR acceptance | Final migration gate with explicit authorization | Act as an edit/compile/debug loop |

Tier numbers express cost and scope, not importance. Run the cheapest test
capable of disproving the change first.

## Current-to-target classification

### Tier 0: compile and static feedback

- targeted `cargo check -p <crate>` and `cargo clippy -p <crate>`;
- Rust formatting/checks for changed crates;
- targeted Meson/Ninja compilation for legacy/native code still involved;
- C/C++ strict compiler checks while legacy targets remain; and
- source-layout/static-schema checks are static feedback conceptually, but the
  existing Meson registrations remain in `tier1-unit` because Meson has no
  compile-only Tier 0 test suite.

Ordinary Cargo checks must not invoke Meson. The combined checkpoint driver
may invoke both graphs explicitly.

### Tier 1: unit and pure behavior

Classify these current families here when they run in-process and use only
deterministic/fake inputs:

- `tests/unit/*`;
- `tests/wm/*` policy, placement, focus, output, transaction, and VRR tests;
- `tests/protocol/*` framing, setup, event, request, reply, and dispatch tests;
- `tests/output/*` headless output, frame staging, and simulated VRR tests;
- `tests/pixel/*` canonical software renderer and golden calculations;
- `tests/drm/*` selector, cache, state, fake API, transaction, serialization,
  timing, and presenter tests that inject fake DRM/KMS APIs;
- unit-style `tests/tools/*_test.cpp` and pure CLI/validator unit tests;
- `tests/compat/m9/*_test.py`, `m11/*_test.py`, `m12/*_test.py`,
  `m13/*_test.py`, and `m14/*_test.py` when they validate supplied data only;
  and
- `tests/hardware/m14/*_test.py`, which tests guard, provenance, evidence, and
  live-runner interpretation with injected fixtures and must remain offline;
  and
- `tests/integration/server_vrr_lifecycle_test.cpp`, which is an in-process
  lifecycle component test despite its historical integration-directory path.

Rust destinations include policy tests in `gwm-core`, renderer/output tests in
`gwcomp-core`, protocol tests in `glasswyrm-x11`, and focused harness tests in
`gw-test-support`.

### Tier 2: contracts, components, and fixtures

- current GWIPC codec/public API tests declared by
  `tests/manifest/ipc/meson.build`;
- `tests/integration/gwipc_*`, including transport, endpoint, malformed peer,
  fault atomicity, edge, and probe coverage;
- C/C++ consumers under `tests/install/` while the installed ABI is retained;
- M6-M14 canonical fixture validators and source/archive manifests;
- fake-server CLI contract tests for `gwinfo`, `gwout`, and output clients;
- component-level server, compositor, DRM, input, and session tests that use an
  injected peer/backend without launching the complete stack; and
- the required cross-language matrix: legacy encode to Rust decode, Rust
  encode to legacy decode, canonical malformed legacy input to Rust rejection,
  and contractual Rust malformed fixtures to legacy rejection.

FD passing, disconnect cleanup, length/overflow rejection, version
negotiation, message IDs, and snapshot/replay are Tier 2 compatibility gates,
not matters to defer to process acceptance.

### Tier 3: headless process integration

- `tests/integration/gwm_process_test.cpp`, `gwm_robustness_test.cpp`,
  `gwm_scenario_matrix_test.cpp`, and `gwm_vrr_process_test.cpp`;
- `gwcomp_process_test.cpp`, `gwcomp_metadata_process_test.cpp`,
  `gwcomp_output_inventory_process_test.cpp`, `gwcomp_scenario_matrix_test.cpp`,
  and the headless golden producer scenarios;
- `glasswyrmd_*`, lifecycle, restart, peer-bootstrap, selection, synthetic
  input/cursor, and supported-client probe scenarios that launch server peers;
- `output_control_peer_process_test.cpp`,
  `compositor_peer_vrr_buffer_release_test.cpp`,
  `output_configuration_process_test.cpp`;
- applicable `tests/apps/*_runtime_test.sh` and integrated headless scripts; and
- mixed legacy/Rust topologies used at an active migration boundary.

Launch only the processes needed for the scenario. Prefer explicit socket/GWIPC
readiness, captured per-process output, bounded polling, and deterministic
teardown over fixed sleeps.

### Tier 4: full software acceptance

- all Tier 0-3 tests in the relevant feature configuration;
- the complete legacy Meson suite while it remains the oracle;
- full Rust workspace tests and lints;
- GCC, Clang, ASan, UBSan, and Rust equivalents applicable to retained code;
- M9-M14 compatibility fixture and evidence validators that do not require
  physical display control;
- software/headless renderer hash and normalized-trace gates;
- installed ABI/tool/package smoke tests; and
- failure-bundle and cleanup verification.

Tier 4 is a phase gate, not the command to rerun after every correction.

### Tier 4-VM: Glasswyrm Gentoo VM

Use the `glasswyrm` VM for packaging, install/uninstall, component rebuild,
supported-client, QXL DRM, VT/restart, and negative-capability gates that do not
require real hardware. Preserve milestone prerequisites and resets specified by
the relevant VM runbook. QXL can prove real kernel/libdrm paths, restoration,
and absence of VRR capability; it cannot prove physical VRR engagement.

`./tools/gw-vm rust-transition-software-test` is the current transition gate
for locked Rust workspace format, check, test, and Clippy validation. It
requires a clean committed checkout, synchronizes that source itself, and
records the tested commit, UTC timestamp, and guest tool versions. It does not
run the Meson suite, the three-process restart oracle, install checks, or
packaging acceptance; those remain separate gates.

Run VM gates from already coherent, locally validated source. Do not use the VM
as the seconds-scale Rust inner loop.

### Tier 5: physical hardware acceptance

Tier 5 consists of live `tools/gw-hw` stages and non-fixture NVIDIA truth-probe
execution against a reviewed physical target. Offline doctor, dry-run,
self-test, analysis, and fixture replay remain lower-tier checks.

Live hardware execution requires all of the following:

- Tier 4 and applicable Tier 4-VM gates are green;
- Mike explicitly authorizes the run;
- `GW_ALLOW_HARDWARE_TESTS=1` is set to the exact value `1`;
- existing literal `--yes`, provenance, doctor/preflight, safe VT, reviewed
  connector, noninteractive authorization, and recovery-access requirements
  pass; and
- the guarded `systemd-run` scope is used where the M14 runbook requires it.

Unset or malformed hardware authorization must fail before the tool validates
the live scope, creates artifacts, accesses devices, or begins session
takeover. A failure at Tier 5 returns to the smallest capable lower tier; it
does not authorize repeated physical runs after each edit.

## R3 priority harness regression

`tests/integration/output_configuration_process_test.cpp` is the first
process-harness modernization target. It exercises the three processes, output
configuration, compositor peer failure, restart/replay, VRR preservation,
queued-work rejection, and later transaction success. The legacy C++ oracle
retains a fixed 100 ms sleep before interpreting post-disconnect behavior and
has only the aggregate process output available on failure.

The first Rust harness is implemented in `crates/gw-transition-tests`. It runs
the unchanged scenario against the legacy processes, observes protocol state
instead of using the fixed sleep, and captures bounded per-process artifacts.
It must remain green at transition checkpoints. Later Rust replacement
topologies must:

1. retain the unchanged legacy expectations;
2. use observable socket/GWIPC readiness;
3. capture individual command lines, stdout/stderr, exit/signal status,
   monotonic event timing, relevant protocol/state snapshots, and fixture
   checksums in a failure bundle;
4. retain focused regressions for harness interpretation; and
5. run the identical scenario against each replacement topology.

Other process fixtures containing `sleep_for`, `sleep`, or `time.sleep` should
be reviewed during R3. A bounded delay can remain when the OS requires it, but
it cannot be the sole readiness proof.

## Differential process checkpoints

| Checkpoint | Topology | Evidence |
|---|---|---|
| Legacy oracle | legacy `glasswyrmd` + legacy `gwm` + legacy `gwcomp` | Accepted canonical fixtures and behavior only |
| GWM replacement | legacy `glasswyrmd` + Rust `gwm` + legacy `gwcomp` | Policy snapshots, events, disconnect, and restart/replay parity |
| Headless compositor replacement | legacy `glasswyrmd` + Rust `gwm` + Rust headless `gwcomp` | Scene/output hashes, M13 scale, M14 policy/state, and peer recovery parity |
| Server replacement | Rust `glasswyrmd` + Rust `gwm` + Rust `gwcomp` | Supported X11 subset, GWIPC, process, and full software acceptance |

Add another topology only when it isolates a specific migration boundary. Do
not build a permanent combinatorial legacy/Rust matrix.

### Retained Rust `gwcomp` checkpoint

Run the active headless-compositor replacement checkpoint with:

```sh
cargo xtask test mixed gwcomp
```

The gate builds the Rust `gwcomp` binary when no candidate directory is
supplied, then invokes the retained legacy-built process probes against that
candidate. It covers listener and peer lifecycle, metadata scene handling,
M13 output inventory, the canonical M4 frame golden, and the accepted headless
scenario matrix. It also runs the accepted M14 headless VRR cadence/runtime
client with the Rust compositor and retained legacy server, window manager,
client, validator, and tools. The golden and scenario probes use the retained
legacy M4 producer, preserving the oracle inputs while changing only the
compositor process under test.

Use `--legacy-build PATH` to select the configured Meson build containing the
probe executables and producer. Use `--rust-bin-dir PATH` to test an already
built Rust candidate without rebuilding it. `cargo xtask test mixed all` also
includes this checkpoint.

The M14 invocation preserves the accepted fixture and validator unchanged. It
is software-only and does not authorize DRM or physical hardware access.

## Required failure artifacts

A Tier 3 or higher failure should retain a bounded bundle containing:

```text
artifacts/test-failures/<test>/<run-id>/
  manifest.json
  command-lines.txt
  process-status.json
  *.stdout
  *.stderr
  protocol-trace.*
  scene-snapshot.*
  relevant-config.*
  fixture-checksums.txt
```

Physical bundles extend this with DRM, driver, output, timing, pre/post state,
provenance, and restoration evidence. Human-readable logs are diagnostic; use
structured values, records, and snapshots for assertions wherever possible.
