# Milestone 14 physical hardware harness

This directory defines the reviewed, local-only input schema for the positive
VRR hardware run. Copy `config.example.toml` outside the source tree and replace
every placeholder with values reviewed for the target monitor and connector.

The schema is deliberately closed. It contains no command, SSH, package,
credential, token, or password fields. `gw-hw` rejects unknown keys, symlinked
configuration files, unsafe device names, invalid ranges, and malformed EDID
digests. Set `tested_commit` to the exact 40-character commit built in the
fixed build directory; the pinned M14 `required_base_commit` and tested commit
must also be repeated on each command line. `keyboard_device` and
`pointer_device` must name two reviewed
`/dev/input/eventN` devices for the isolated session; the doctor verifies both
character devices before the live runner stops the selected getty. `tty` must
be the active VT in `KD_TEXT`; `alternate_tty` must be a distinct, inactive
`KD_TEXT` VT that is safe for the bounded release/acquire check. The doctor
also refuses profiles with more than one connected or active physical
connector.

The live runner repeats the active-VT and `KD_TEXT` checks for both configured
VTs at takeover time and again immediately before stopping the getty. Cleanup
records the exact before/after active VT, KD mode, and getty state; a failed
run reports the expected and observed values for every restoration mismatch.

The fixed live runner uses binaries from `/var/tmp/glasswyrm-build-m14` and
refuses symlinked, missing, or unproven executables. Configure the exact clean
commit with the provenance option, then build it before moving to the target
text VT. Use an empty directory, or add `--reconfigure` when refreshing an
existing fixed build after `HEAD` changes:

```sh
meson setup /var/tmp/glasswyrm-build-m14 \
  --buildtype=debugoptimized \
  -Ddrm_backend=true -Dlibinput_backend=true \
  -Dphysical_validation_provenance=true
meson compile -C /var/tmp/glasswyrm-build-m14
```

The optimized build type is required because the positive gate measures
physical 4K presentation cadence; the separate debug and sanitizer builds
remain the correctness gates. The opt-in Meson target records the configured
Git commit and refuses to emit
`glasswyrm-m14-build-manifest.json` if `HEAD` changed or any tracked source is
dirty. It hashes `gwm`, `gwcomp`, `glasswyrmd`, `gwout`, `gwinfo`, the M14 XCB
client, and `gw_drm_probe`. Untracked local plans do not invalidate the build.
`gw-hw` independently checks the exact fixed paths, regular-executable type,
size, and SHA-256 digest against `--tested-commit` before live hardware
discovery. It copies the validated manifest into the evidence archive.

```sh
./tools/gw-hw doctor --config /path/to/reviewed.toml \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit COMMIT
systemd-run --scope --unit=glasswyrm-m14-harness \
  --collect --quiet -- \
  ./tools/gw-hw milestone14-vrr-test \
  --config /path/to/reviewed.toml \
  --required-base 6864ea631d61636289a21c7d2d6655a17be0c004 \
  --tested-commit COMMIT \
  --artifact-dir /var/tmp/glasswyrm-m14-hardware --yes
```

`doctor` is bounded discovery. The fixed transient scope is mandatory: a
direct live invocation is rejected before artifact creation or hardware
takeover because stopping a getty-owned login shell would otherwise terminate
the harness itself. The scope isolates the harness from the configured getty;
the runner ignores only the resulting terminal hangup while it performs its
unconditional cleanup and exact restoration checks.

The live command is deliberately disruptive: it
may take DRM master, stop the configured getty, switch VTs, and reconfigure the
selected display. Invoke it only from the configured spare text VT, with the
display manager inactive and independent recovery access available. Do not run
it from the current graphical session or a TTY whose interruption would be
noticeable.

`range_source` is reported as `debugfs` only when the connector debugfs data
contains one unambiguous labelled minimum/maximum refresh pair that exactly
matches the reviewed configuration. Missing, malformed, ambiguous, or
mismatched debugfs ranges fall back to the explicit `config-reviewed` source.

The implementation includes deterministic parser, doctor, provenance,
failure-unwind, and fixture dry-run tests. Those are not positive hardware
proof. Only a reviewed live run whose build hashes, cadence, property readback,
images, restoration, checksums, and archive validators all pass can satisfy the
physical gate. The archived
AppRequested evidence must show Default disabled, Prefer effectively enabled,
and Disable effectively disabled in compositor-authoritative snapshots; no
such acceptance is claimed by this document.

The live compositor uses one-shot mirror capture. Its two 180-frame cadence
workloads produce timing and DRM reports but no PPM files or mirror `fsync`
traffic. After cadence collection and restart recovery, the runner explicitly
converges compositor-authoritative policy, arms exactly one mirror, requests a
bounded repaint from the held client, and does this once with VRR off and once
with VRR enabled. It compares the resulting canonical pixels and removes any
unconsumed runtime trigger during cleanup. This bounds a 4K run to two
full-size PPM proof frames while preserving the default
`gwcomp --mirror-dump-dir` behavior for other workflows.
