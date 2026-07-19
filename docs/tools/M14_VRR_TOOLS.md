# Milestone 14 VRR tools

M14 extends the existing same-UID output-control and diagnostic socket. It does
not add a second privileged control plane.

## gwout

Set an output's complete-layout VRR policy with:

```sh
gwout --socket PATH set OUTPUT --vrr off
gwout --socket PATH set OUTPUT --vrr fullscreen
gwout --socket PATH set OUTPUT --vrr focused
gwout --socket PATH set OUTPUT --vrr app-requested
gwout --socket PATH set OUTPUT --vrr always-eligible
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
gwinfo --socket PATH outputs --vrr --json
gwinfo --socket PATH windows --vrr --json
gwinfo --socket PATH all --vrr --json
```

Output includes policy, property presence, hardware capability,
controllability, simulation, range, decision, desired and effective state,
candidate, transition serial, kernel flip timestamp, interval, and stable
ordered reason names. JSON also includes per-window surface, preference,
eligibility, selection, focus, fullscreen/borderless, exclusive membership,
generation, and reasons.

`--vrr` is an explicit schema opt-in for the historical `outputs`, `windows`,
and `all` commands. It appends a nested `vrr` object (or `null` when the
negotiated snapshot has no matching record). Without that modifier, their JSON
and text bytes remain exactly historical. The dedicated `vrr` command already
requests that schema and therefore rejects the redundant modifier. A
historical peer that did not negotiate VRR receives no VRR records.

The C/C++ policy and report vocabulary uses the canonical CamelCase reason
registry frozen in `vrr-reasons.json`, such as `WindowDidNotRequest`.
Command-line JSON and text deliberately present the same bits as stable
kebab-case names, such as `window-did-not-request`. This is a one-to-one
presentation mapping; bit ordering and precedence do not change.

## gwcomp diagnostics

`gwcomp --vrr-report PATH` creates a private new JSONL report and refuses to
replace an existing path. It must be distinct from `--drm-report`. Headless
simulation is configured with repeatable
`--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ`; names must be unique and refer
to configured headless outputs, and the maximum cannot exceed nominal refresh.

The headless report uses separate `capability`, `decision`, `timing`, `summary`,
and `restore` records. DRM reports use the corresponding names prefixed with
`vrr-`. Both exclude wall-clock time and do not alter frame manifests or pixel
hashes.

## Validation harnesses

`gw-vm milestone14-runtime-test --yes` is the fixed QXL negative-capability
gate. `gw-hw doctor --config PATH` validates a reviewed physical target, and
`gw-hw milestone14-vrr-test --config PATH --artifact-dir PATH --yes` is the
fixed positive gate. The hardware command may take DRM master, switch VTs,
stop the selected getty, and reconfigure the display. Never run it from the
current graphical session or a TTY whose interruption is unacceptable; follow
the safety requirements in the M14 hardware validation document first.
