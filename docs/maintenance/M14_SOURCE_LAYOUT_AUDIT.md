# Milestone 14 source-layout audit

Status: implementation layout passes the source guard; VM and physical-display
acceptance remain independent pending gates.

Required baseline: `6864ea631d61636289a21c7d2d6655a17be0c004`

## Guard, inventory, and limits

`tests/tools/source_layout_test.sh` classifies every C/C++ source and header
under `src/` against the required Milestone 13 baseline. Physical lines include
comments and blanks. The current implementation inventory is 495 files and
71,746 lines, compared with 439 files and 62,452 lines at the baseline. The
M14 delta contains 145 changed source/header paths: 56 added and 89 modified,
with 9,805 added and 511 removed physical lines.

The guard enforces the actual current limits below:

| Classification | Limit | Current evidence |
| --- | ---: | --- |
| Unchanged source/header hard default | 1,000 | Largest current file is `src/glasswyrmd/policy_peer.cpp` at 663 lines. |
| New or materially rewritten source/header | 600 | Largest new file is `src/ipc/wire/vrr_contract.cpp` at 474 lines. |
| `src/glasswyrmd/server.cpp`, `runtime.cpp`, and `*_coordinator.cpp` | 500 | No coordinator violation. |
| Any `main.cpp` | 250 | No top-level-main violation. |
| `src/glasswyrmd/request_dispatcher.cpp` | 450 | 186 lines. |
| `src/glasswyrmd/resource_table.cpp` and `server.cpp` | 500 | 242 and 92 lines respectively. |
| `src/gwcomp/compositor.cpp` | 600 | 479 lines. |
| `src/ipc/connection.cpp` | 350 | 70 lines. |
| Ordinary function design target | 100 | Advisory only. |
| Function review threshold | greater than 150 | Eleven reviewed findings below. |

The current guard reports `source-layout: PASS (11 function review item(s))`
with no file-budget, coordinator, main, routing-shell, stale-entry, or missing-
allowlist failure.

## Empty allowlist evidence

`docs/maintenance/source_size_allowlist.txt` contains only its five-field
format comments. This check returns no active line:

```sh
rg -n -v '^\s*(#|$)' docs/maintenance/source_size_allowlist.txt
```

No M14 file is allowlisted. The guard also rejects new-file exceptions, stale
line counts, duplicate entries, and any exception outside `src/` C/C++ files.

## Milestone 14 decomposition

| Responsibility | Primary implementation areas | Boundary |
| --- | --- | --- |
| Pure policy, reasons, and timing | `src/output/vrr/` | Component-neutral decisions and stable reason precedence have no IPC, X11, or KMS ownership. |
| DRM capability and property state | `src/backends/drm/vrr_*`, `kms_vrr_state.*`, `presenter_vrr*`, `drm_vrr_report.*` | Discovery, TEST_ONLY negotiation, atomic mutation/readback, timing, and evidence are separate from policy. |
| Headless simulation | `src/backends/headless/vrr_simulation.*`, `vrr_report.*` | Explicit named-output simulation never becomes a physical capability claim. |
| Public and wire contracts | `include/glasswyrm/ipc/vrr.h`, `src/ipc/wire/vrr_contract.*`, focused connection validation/correlation | API 0.9 codecs and semantic rules remain additive to wire 1.0 and historical API versions. |
| Window policy | `src/wm/vrr_*`, `src/gwm/contract_vrr.*` | GWM owns deterministic candidate classification and selection, not surfaces or KMS. |
| X11 preference and transaction state | `src/glasswyrmd/vrr_*`, `extensions/gw_vrr.*`, lifecycle/output-control projections | The server owns preference, committed policy/cache, notifications, promotion, and rollback. |
| Compositor validation and response | `src/compositor/vrr_state.*`, `scene_vrr_validation.*`, `src/gwcomp/vrr_*` | `gwcomp` validates surfaces, drives presentation, and publishes effective state/timing. |
| Diagnostics and control | `tools/output_client/`, `tools/gwinfo/`, `tools/gwout/` | Shared snapshot/edit/format code avoids separate VRR control protocols. |
| Acceptance harnesses | `tests/compat/m14/`, `tests/hardware/m14/`, `tools/gw-vm.d/lib/milestone14.sh`, `tools/gw-hw` | Host, QXL negative-capability, and physical positive gates remain distinct evidence boundaries. |

The largest new modules are the 474-line VRR wire codec, 398-line server cache,
and 302-line server policy projection. They remain focused and below the
600-line new-file cap. Existing process/peer files above 600 lines remain below
the 1,000-line default and were not classified as materially rewritten by the
baseline-aware guard.

## Reviewed function spans

The lexical scan reports eleven approximate spans above 150 lines. These are
review findings, not allowlist exceptions:

| Function or area | Approximate span | Review disposition |
| --- | ---: | --- |
| `SceneModel::commit` | 177 | Owns one atomic scene validation/promotion boundary; VRR validation remains in `scene_vrr_validation.*`. |
| `CompositorPeer::drain` | 165 | Keeps ordered peer receive, inventory bootstrap, response validation, and disconnect outcome in one state-machine loop. |
| `ServerRuntime::service_input` | 169 | Preserves the ordered synthetic-input transaction and focus/cursor publication; device and routing semantics are delegated. |
| `ServerRuntime::complete_lifecycle` | 189 | Owns one lifecycle completion, deferred mutation, VRR rollback/promotion, cleanup, and notification boundary. |
| `dispatch_request` | 172 | Deliberately flat core/extension routing shell; request semantics live in focused handlers. |
| `RuntimeBridge::service` | 248 | Atomically coordinates the policy/compositor peer pair, staged retries, and transaction result state. |
| `gwcomp::parse_options` | 202 | Parses one bounded CLI vocabulary; backend, headless-VRR, DRM, renderer, and report validation are delegated helpers. |
| IPC correlation family near `connection_correlation.cpp:16` | 245 | The lexical scanner begins at forward declarations; actual request/reply correlation remains split into focused helpers. |
| IPC validation family near `connection_validation.cpp:22` | 332 | The lexical scanner groups declarations and the flat capability/direction dispatcher; VRR payload semantics remain in the dedicated codec. |
| `MultiOutputGlesSceneRenderer::render_output` | 191 | Owns one output's ordered GLES composition while mapping, upload, and geometry helpers remain separate. |
| session `parse_options` | 161 | Parses and validates one fixed launcher vocabulary; process construction and supervision remain separate. |

Any further growth of these spans requires another decomposition review rather
than an allowlist entry.

## Proof boundary

This audit proves only repository organization and the empty exception list.
It does not prove a clean build, historical compatibility, QXL behavior, real
connector capability, cadence, VT safety, or exact restoration. Those remain
separate strict-build, regression, clean-VM, and reviewed physical-display
gates. No M14 completion or hardware-acceptance claim follows from this audit.
