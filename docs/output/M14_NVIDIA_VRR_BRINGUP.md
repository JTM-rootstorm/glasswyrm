# Milestone 14 NVIDIA VRR bring-up

This procedure separates host preparation from Glasswyrm source diagnosis.
The read-only doctor is L3 evidence. It must pass before the minimal L4 KMS
probe or any complete physical run is considered.

## Human-owned preparation

The operator owns boot configuration, initramfs regeneration, NVIDIA module
reloads, rebooting, package changes, monitor controls, cables, and display
manager state. Glasswyrm tooling observes those facts and fails closed; it does
not change them.

For NVIDIA DRM, load-time vblank notifications must be enabled with
`nvidia_drm.vblank=1`. The doctor also records `nvidia_drm.modeset`, optional
`nvidia_drm.fbdev`, the loaded driver version and module flavor, and optional
`nvidia_modeset.conceal_vrr_caps`. Positive testing requires observed
capability concealment to be disabled. If the concealment parameter is absent,
the doctor records `unavailable` rather than guessing.

## Read-only classifications

The doctor report contains a stable `failure_reasons` array derived from its
individual checks. Use the first failed reason to choose the next action:

- vblank, concealment, active-VT, getty, session permission, or competing
  master failures are host prerequisites and do not justify a source rebuild;
- connector, EDID, mode, or capability failures require display, driver, and
  reviewed-profile investigation;
- malformed or absent `VRR_ENABLED` remains a DRM capability failure;
- exact off/on atomic controllability belongs to the non-mutating preflight of
  the L4 probe, not to the L3 doctor.

The doctor never writes sysfs, module parameters, boot files, KMS state, VT
state, input devices, or package state.

## Progression

After L3 passes, run only the minimal L4 NVIDIA VRR truth probe. Advance to the
three-process cadence stage only when that probe reports
`accepted-probe-distinction`. Property readback without distinguishable raw
page-flip cadence is not positive M14 evidence.

## Minimal L4 build and run

Freeze and commit the candidate before configuring the separate probe build.
The opt-in build target records that exact clean `HEAD`, hashes only
`gw_drm_vrr_probe`, and refuses a tracked-dirty source tree. It does not create
or modify the final L8 `/var/tmp/glasswyrm-build-m14` build.

```sh
meson setup /var/tmp/glasswyrm-build-m14-nvidia-probe \
  --buildtype=debugoptimized \
  -Ddrm_backend=true \
  -Dm14_nvidia_probe_provenance=true
meson compile -C /var/tmp/glasswyrm-build-m14-nvidia-probe \
  gw_drm_vrr_probe m14-nvidia-probe-build-provenance
```

If that build directory was configured at an earlier commit, run `meson setup
--reconfigure` with the same options after checking out the reviewed candidate,
then compile the two named targets again. The generated manifest and executable
must remain regular files; the wrapper rejects symlink substitution, a changed
commit, or a hash mismatch.

From the reviewed active text VT, with the display manager and any competing
DRM master stopped, run:

```sh
systemd-run --scope --unit=glasswyrm-m14-harness \
  --collect --quiet -- \
  ./tools/gw-hw milestone14-nvidia-vrr-probe \
    --config /path/to/reviewed.toml \
    --artifact-dir /var/tmp/glasswyrm-m14-nvidia-probe-artifacts \
    --yes
```

The artifact directory must be new or empty, private, and nonsymlinked. The
wrapper checks the exact connector, EDID, mode, active text VT, NVIDIA facts,
DRM master availability, probe build provenance, and literal confirmation
before invoking the fixed binary. Input devices are intentionally not required
because L4 does not start `glasswyrmd`, `gwm`, `gwcomp`, or a client. The C++
probe restores the saved KMS state before releasing its buffers; success also
requires the offline analyzer to accept the restoration record and the frozen
off/on cadence distinction.

## Offline replay

The repository analyzer consumes a completed probe JSONL without opening a DRM
node or changing host state:

```sh
./tools/gw-hw analyze-milestone14-nvidia-vrr-probe \
  --report RAW.jsonl \
  --config REVIEWED.toml \
  --output SUMMARY.json
```

The output path must not already exist. The analyzer bounds report bytes and
record counts, requires exact start/flip/restore schemas, rejects wall-clock
fields, mixed CRTC identities, duplicate or discontinuous ordinals, timestamp
regressions, readback divergence, and incomplete restoration. CRTC-sequence
and legacy-vblank samples remain serialized diagnostics but never contribute
to the acceptance percentage.

Only raw page-flip timestamps feed the frozen 120-interval, 75-percent enabled,
and below-25-percent disabled thresholds. A valid negative classification is a
useful blocker result; only `accepted-probe-distinction` returns success and
authorizes L5.
