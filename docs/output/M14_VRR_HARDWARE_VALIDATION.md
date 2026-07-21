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
The live runner accepts only nonsymlink executables from the fixed
`/var/tmp/glasswyrm-build-m14` build.

The accepted one-output profile requires exactly one connected connector and
exactly one active connector, both the configured connector. The doctor claims
a `debugfs` range source only after parsing one labelled minimum/maximum pair
that exactly matches the reviewed values; otherwise the explicitly reviewed
configuration remains the recorded source.

The live command requires an explicit confirmation token:

```sh
tested_commit=$(git rev-parse HEAD)
./tools/gw-hw doctor --config PATH \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-doctor
./tools/gw-hw milestone14-vrr-test \
  --config PATH \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit "$tested_commit" \
  --artifact-dir /var/tmp/glasswyrm-m14-hardware --yes
```

The doctor currently requires the libdrm `modetest` executable at
`/usr/bin/modetest` or `/bin/modetest` for exact mode and property discovery.

Before using it, arrange console access and recovery independent of the tested
display. Record the current KMS, VRR, KD, VT, getty, device, and session state.
If the doctor or any later stage fails, the restoration guard stops child
processes, disables VRR where possible, restores saved state, and validates
readback before returning failure.

## Required behavior

The fixed run exercises policy Off, Fullscreen enter/exit,
borderless-fullscreen, Focused, AppRequested Default/Prefer/Disable,
AlwaysEligible, VT release/acquire, GWM restart, compositor restart, and clean
shutdown. It captures property readback and kernel intervals for the same
in-range target with VRR off and on. The cadence thresholds are defined in
[M14 VRR timing](M14_VRR_TIMING.md).

The selected positive profile remains scale 1, transform Normal, one physical
output, and the existing composited primary-plane path. QXL, headless
simulation, a virtual display, legacy KMS, or property readback without cadence
evidence cannot satisfy this gate.

## Evidence

The archive contains the reviewed configuration without secrets, EDID hash,
kernel/libdrm/driver facts, capability and property snapshots, all decision and
timing records, off/on summaries, policy transition logs, VT/restart evidence,
canonical and screen images, before/after KMS and session state, restoration
results, and `SHA256SUMS`. Validators reject missing fields, wall-clock data,
an unconfirmed hardware path, insufficient samples, failed thresholds, or an
incomplete restore.

The implementation and deterministic dry-run coverage do not constitute a
positive result. Until a reviewed live archive passes these checks, Milestone
14 must be described as hardware-acceptance pending.
