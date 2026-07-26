# Milestone 14 physical VRR validation

The positive gate is intentionally separate from host, fake-DRM, and QXL VM
testing. It must run on one reviewed atomic-KMS connector and display from a
dedicated text-only Glasswyrm session. It may seize DRM master, switch VTs,
and reconfigure the selected output; do not run it from an active graphical
session.

## Safety boundary

`gw-hw doctor --config PATH` performs bounded discovery and rejects a target
unless the exact primary node, connector, EDID hash, mode, active text VT,
distinct inactive text VT, reviewed
refresh range, distinguishable target cadence, and two reviewed
`/dev/input/eventN` devices match. It also rejects a competing DRM master or an
unsafe configuration file. The configuration has a fixed schema and cannot
contain commands, package operations, passwords, or remote execution fields.
The live runner accepts only regular, nonsymlink executables from the fixed
`/var/tmp/glasswyrm-build-m14` build whose sizes and SHA-256 digests match its
Meson-generated exact-commit manifest.

The accepted one-output profile requires exactly one connected connector and
exactly one active connector, both the configured connector. The doctor claims
a `debugfs` range source only after parsing one labelled minimum/maximum pair
that exactly matches the reviewed values; otherwise the explicitly reviewed
configuration remains the recorded source.

The live command requires an explicit confirmation token:

```sh
tested_commit=$(git rev-parse HEAD)
meson setup /var/tmp/glasswyrm-build-m14 \
  --buildtype=debugoptimized \
  -Ddrm_backend=true -Dlibinput_backend=true \
  -Dphysical_validation_provenance=true
meson compile -C /var/tmp/glasswyrm-build-m14
./tools/gw-hw doctor --config PATH \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-doctor
systemd-run --scope --unit=glasswyrm-m14-harness \
  --collect --quiet -- \
  ./tools/gw-hw milestone14-vrr-test \
  --config PATH \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-hardware --yes
```

For a reviewed detached launch, add `--unattended` to the final command. This
permits an automation process whose stdin is not the configured Linux VT, but
does not permit a different active console: the root doctor and just-in-time
preflight still read the kernel active-VT and `KD_TEXT` state directly. The
runner converts ordinary termination signals into a restoration failure and
executes its cleanup guard before exiting. A kernel/driver failure, power
loss, or `SIGKILL` still requires the independent recovery route.

The provenance option is deliberately opt-in. Its build-by-default target
requires the source tree to remain at the configured `HEAD` with no tracked
changes, then hashes every repository executable used by the physical runner.
Reconfigure the fixed build after changing commits so Meson records the new
candidate `HEAD`.
The doctor rejects a missing manifest, a different tested commit, an unexpected
binary set or path, and any size or SHA-256 mismatch before inspecting the
selected hardware. The same validated manifest is archived as
`milestone14-build-provenance.json`; archive validation binds its source commit
to the reviewed configuration, doctor report, and final summary.

The doctor currently requires the libdrm `modetest` executable at
`/usr/bin/modetest` or `/bin/modetest` for exact mode and property discovery.

Every fixed transient service writes standard output and standard error to a
private, per-unit `*.log` file in the live artifact directory. These diagnostic
sidecars survive early service failure without weakening the accepted
archive's exact artifact allowlist.

Snapshot observation uses the output client's bounded readiness retry rather
than launching hundreds of fresh `gwinfo` processes. The harness permits at
most three snapshot convergence queries and five client-cleanup queries. It
records exit status, terminating signal, timeout state, byte counts, elapsed
time, output path, and a bounded output tail in private
`.query-diagnostics/*.diagnostic.json` files after a failed or slow query.
Malformed or zero-byte successful output fails immediately, while a coherent
but not-yet-converged JSON snapshot is retained privately for diagnosis.
Any other failed fixed command also records its exact argument vector and the
same bounded execution evidence in a sequential private
`.command-diagnostics/*.diagnostic.json` sidecar. This includes `gwout` policy
transitions, so an unattended failure retains the rejection or connection
detail even when the live runner must proceed directly to restoration.

Before takeover, the live runner checks its bounded core and client unit-name
set. Active collisions are rejected; inactive or failed stale transient units
are reset by exact name and must unload before the run proceeds. New units use
`Type=exec` startup confirmation and collection-on-failure, while cleanup stops
only units whose transient launch succeeded. A compositor restart creates a
fresh transient service instead of relying on an unloaded definition.

The live command refuses direct execution and requires the exact
`glasswyrm-m14-harness.scope` transient scope shown above. This prevents
stopping the configured getty from terminating the harness with its login
shell. The detached runner ignores the getty's terminal hangup and retains its
unconditional cleanup guard. Interactive execution requires stdin to be the
configured active physical text VT; `--unattended` replaces only that identity
check while retaining the kernel active-VT and `KD_TEXT` checks.

Before using it, arrange console access and recovery independent of the tested
display. Record the current KMS, VRR, KD, VT, getty, device, and session state.
If the doctor or any later stage fails, the restoration guard stops child
processes, disables VRR where possible, restores saved state, and validates
readback before returning failure. The live preflight rechecks the configured
active VT and verifies `KD_TEXT` on both configured VTs immediately before
takeover. Failure artifacts preserve exact before/after VT, KD, and getty
values and report each mismatch without masking the primary failure.

## Required behavior

Before any physical run, `m14-bounded-damage-fake-drm` exercises a
2560x1440@120000 in-process fake DRM/KMS target. After the two scanout buffers
are seeded, 180 steady 64x64 updates must avoid full-copy fallbacks, remain at
or below 256 KiB per copy, and total less than ten percent of the equivalent
full-frame copies. Steady updates must also report zero direct scanout-readback
bytes while retaining full logical parity through the seeded per-buffer damage
lineage. The fixture proves skipped-generation history union, incomplete
advertised-damage detection, exact canonical/scanout parity, and complete-copy
direct readback during recovery.

The fixed run exercises policy Off, Fullscreen enter/exit,
borderless-fullscreen, Focused, AppRequested Default/Prefer/Disable,
AlwaysEligible, VT release/acquire, GWM restart, compositor restart, and clean
shutdown. It captures property readback and kernel intervals for the same
in-range target with VRR off and on. The cadence thresholds are defined in
[M14 VRR timing](M14_VRR_TIMING.md).

Some DRM drivers deliver a valid page-flip event and strictly increasing
monotonic kernel timestamps while leaving the event sequence at zero.
Glasswyrm records that zero unchanged and accepts consecutive zero-sequence
events only when their kernel timestamps increase. The real DRM adapter may
separately sample the current 64-bit CRTC sequence for a zero-sequence event,
but it does not synthesize or replace the raw event fields. Only a successful,
monotonic query whose timestamp tightly correlates with that event is marked
cadence-eligible. The standard DRM `flip` record archives that separately
tagged diagnostic, but M14 cadence acceptance does not yet consume it. Missing
or regressed event timestamps remain nonfatal runtime timing loss and cannot
satisfy physical cadence acceptance.
The cadence client requests the matching window projection with each VRR timing
snapshot so an enabled candidate remains self-contained and coherence-checked.

AppRequested evidence requires exact compositor-authoritative rejection reason
sets: Default and Disable each leave the output at `no-candidate`. Default
carries `window-did-not-request`; Disable carries both
`window-preference-disabled` and `window-did-not-request`, preserving every
applicable reason while `window-preference-disabled` remains primary. Prefer
has no rejection reasons.
Nonblocking timing or hardware-confirmation diagnostics remain independently
visible and do not weaken this exact rejection check.
The corresponding protocol-client evidence also preserves exact nonzero
rejection reason masks and committed notification change masks.

The selected positive profile remains scale 1, transform Normal, one physical
output, and the existing composited primary-plane path. QXL, headless
simulation, a virtual display, legacy KMS, or property readback without cadence
evidence cannot satisfy this gate.

The M14 acceptance profile may use a reviewed non-native mode that preserves
the display's VRR range and target-cadence distinction. Native 4K cadence is a
project requirement, but sustained 4K performance is deferred until the
accelerated rendering path; the scalar software reference renderer is not used
to make that performance claim.

## Evidence

The archive contains the reviewed configuration without secrets, exact-commit
build manifest and executable hashes, EDID hash, kernel/libdrm/driver facts,
capability and property snapshots, standard DRM records, all decision and
timing records, mirror manifests, off/on summaries, policy transition logs,
VT/restart evidence, canonical and screen images, before/after KMS and session
state, restoration results, and `SHA256SUMS`. Validators reject missing fields,
wall-clock data, an unconfirmed hardware path, insufficient samples, failed
thresholds, or an incomplete restore.

Every M14 presentation carries one identity made from output, commit,
generation, and presentation token. The standard DRM and VRR streams publish
that identity beside their frame records. An optional mirror capture publishes
the same output/commit/generation identity plus its frame, filename, and pixel
hash. Only after all required streams commit does the VRR report publish the
final evidence seal. Live and archive validation cross-match the seal against
the standard DRM stream and, when present, the mirror manifest; unsealed
records remain diagnostic only and never contribute cadence or policy
acceptance.

Missing, invalid, or regressed page-flip timestamps are timing-evidence
degradation rather than display-session failure. The completed flip, pixels,
property readback, and frame acknowledgement remain valid, while the timing
sample is marked unavailable and excluded. The hardware gate still fails
closed when too few sealed valid intervals remain. Event identity errors,
property-readback divergence, scanout mismatch, report publication failure,
and restoration failure remain fatal.

Each physical pixel capture first waits for the requested policy and effective
state to converge, then arms the one-shot mirror trigger and requests one
bounded deterministic repaint from the held client. This prevents a stale
pre-transition frame from satisfying either side of the VRR-only comparison.

The held client accepts exactly three bounded repaint requests. The runner
uses the first immediately after returning to the configured active VT and
waits for its private trigger to be consumed before taking the active VRR
snapshot. This forces active-state reevaluation through a new normal
compositor transaction. The other two requests remain dedicated to the
VRR-off and VRR-enabled pixel captures.

The implementation and deterministic dry-run coverage do not constitute a
positive result. Until a reviewed live archive passes these checks, Milestone
14 must be described as hardware-acceptance pending.
