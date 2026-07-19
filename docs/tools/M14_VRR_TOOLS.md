# Milestone 14 VRR tools

M14 extends the existing same-UID output-control and diagnostic socket. It does
not add a second privileged control plane.

## gwout

Set an output's complete-layout VRR policy with:

```sh
gwout --socket PATH OUTPUT --vrr off
gwout --socket PATH OUTPUT --vrr fullscreen
gwout --socket PATH OUTPUT --vrr focused
gwout --socket PATH OUTPUT --vrr app-requested
gwout --socket PATH OUTPUT --vrr always-eligible
```

`OUTPUT` is the stable name or hexadecimal/decimal stable ID. The edit starts
from a complete queried output snapshot and changes only the selected policy.
A non-Off request is rejected locally when the negotiated capability reports
no controllable VRR. Server acknowledgement follows GWM reevaluation,
compositor acceptance, presenter completion, and effective-state readback.
Failure preserves the prior complete layout and policy.

## gwinfo

Inspect all or one output with:

```sh
gwinfo --socket PATH vrr
gwinfo --socket PATH vrr OUTPUT
gwinfo --socket PATH vrr OUTPUT --json
```

Output includes policy, property presence, hardware capability,
controllability, simulation, range, decision, desired and effective state,
candidate, transition serial, kernel flip timestamp, interval, and stable
ordered reason names. JSON also includes per-window surface, preference,
eligibility, selection, focus, fullscreen/borderless, exclusive membership,
generation, and reasons.

Historical commands and queries remain unchanged. A historical peer that did
not negotiate VRR receives no VRR records.

## gwcomp diagnostics

`gwcomp --vrr-report PATH` creates a private new JSONL report and refuses to
replace an existing path. It must be distinct from `--drm-report`. Headless
simulation is configured with repeatable
`--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ`; names must be unique and refer
to configured headless outputs, and the maximum cannot exceed nominal refresh.

The report uses separate capability, decision, timing, summary, and restore
records. It excludes wall-clock time and does not alter frame manifests or
pixel hashes.

## Validation harnesses

`gw-vm milestone14-runtime-test --yes` is the fixed QXL negative-capability
gate. `gw-hw doctor --config PATH` validates a reviewed physical target, and
`gw-hw milestone14-vrr-test --config PATH --yes` is the fixed positive gate.
The hardware command may take DRM master and switch VTs; follow the safety
requirements in the M14 hardware validation document before running it.
