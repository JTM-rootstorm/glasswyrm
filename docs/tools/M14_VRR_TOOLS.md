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

The shared output client exposes one-attempt results as complete, retryable
not-ready, or fatal. Command-line queries retain one connection and retry a
server `Busy` response with a monotonic five-second deadline and explicit
backoff. A closed transport is destroyed before a later query reconnects;
closure, malformed replies, and incomplete snapshots are never reported as
success. Successful JSON output is nonempty, parseable, and newline-terminated.

## gwcomp diagnostics

`gwcomp --vrr-report PATH` creates a private new JSONL report and refuses to
replace an existing path. It must be distinct from `--drm-report`. Headless
simulation is configured with repeatable
`--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ`; names must be unique and refer
to configured headless outputs, and the maximum cannot exceed nominal refresh.

The headless report uses separate `capability`, `decision`, `timing`, `summary`,
and `restore` records. DRM reports use the corresponding names prefixed with
`vrr-`. Timing and summary records contain raw intervals and nominal-mode
facts, never an application-cadence verdict. Both exclude wall-clock time and
do not alter pixel hashes.

During sealed physical reporting, the standard DRM and VRR reports carry
matching `evidence-stream` identities and the VRR report receives an
`evidence-seal` only after every required report and optional mirror artifact
commits. The hardware validator archives and cross-checks the standard DRM and
mirror reports and ignores any unsealed presentation.

DRM report transactions are validated in memory and append only after a
presentation completes. Appends preserve the private report inode and are
flushed as one durability boundary after shutdown summary and restoration
records are written. A crash or partial run therefore remains fail-closed:
missing final summary/restoration records cannot satisfy physical acceptance.

## Validation harnesses

`gw-vm milestone14-runtime-test --yes` is the fixed QXL negative-capability
gate. Both `gw-hw doctor` and `gw-hw milestone14-vrr-test` require the reviewed
configuration, pinned `--required-base`, exact `--tested-commit`, and a private
artifact directory; the live command additionally requires literal `--yes`.
The fixed physical build must be configured with
`-Dphysical_validation_provenance=true`. Its generated manifest binds the clean
configured Git commit to SHA-256 hashes for every repository binary used by the
runner; `gw-hw` re-hashes them before doctor discovery and archives the validated
manifest.
The hardware command may take DRM master, switch VTs, stop the selected getty,
and reconfigure the display. Never run it from the current graphical session
or a TTY whose interruption is unacceptable; follow the safety requirements
in the M14 hardware validation document first. The live command must run in
the fixed `glasswyrm-m14-harness.scope` transient scope documented there;
direct execution is rejected before artifact creation or hardware takeover.

An explicitly detached operator may add `--unattended`. This changes only the
invoking terminal check so a reviewed remote launcher or automation process
does not have to own the configured Linux VT:

```sh
systemd-run --scope \
  --unit=glasswyrm-m14-harness \
  --collect --quiet -- \
  ./tools/gw-hw milestone14-vrr-test \
    --config PATH \
    --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
    --tested-commit "$tested_commit" \
    --artifact-dir /var/tmp/glasswyrm-m14-hardware \
    --unattended --yes
```

Detached execution does not relax hardware safety. The root doctor and live
preflight still require the configured VT to be the kernel-active `KD_TEXT`
console, require the alternate VT to remain `KD_TEXT`, and reject a display
manager, competing DRM master, connector mismatch, or provenance mismatch.
Ordinary `SIGINT` and `SIGTERM` interruptions enter the same restoration guard
as command failures. Kernel or driver failure, power loss, and `SIGKILL`
remain outside userspace recovery; independent console access is still
mandatory.
