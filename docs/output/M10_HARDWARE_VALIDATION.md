# Milestone 10 Hardware Validation

## Acceptance status

Milestone 10 is validated on the configured Gentoo QXL guest. The accepted
route uses `/dev/dri/card0`, connector `Virtual-1`, an exact 1024x768 mode, QXL
atomic KMS, two XRGB8888 dumb buffers, `/dev/tty2`, and libvirt's graphical
console. It proves the M9-clean prerequisite from the single internal `base`
snapshot before installing libdrm for the separate M10 run.

Acceptance requires every artifact and result described below; the summary's
tested commit is the authoritative identity for a particular run. Mocked tests
alone remain insufficient.

## VM prerequisites

The configured libvirt guest must provide all of the following before the M10
scenario starts:

- an x86_64 Gentoo systemd guest at the accepted M9-clean source snapshot;
- a libvirt graphical adapter with a kernel DRM driver already installed and
  loaded;
- exactly identified `/dev/dri/cardN`, connector, and 1024x768 mode;
- dumb-buffer support and evented KMS page flips;
- a free `/dev/ttyN`, reachable SSH for out-of-band control, and a working
  graphical-console screenshot path;
- no Xorg, Xwayland, Xvfb, Mesa, libinput, or display manager; and
- permission for the fixed scenario to install libdrm only.

Installing libdrm must not pull Mesa or an X server. Kernel replacement,
libvirt display-model changes, destructive snapshot work, and permission
changes are prerequisites managed outside the M10 scenario.

For a QXL-backed 1024x768 VM, configure at least 65536 KiB of `vgamem`.
Smaller apertures can exhaust QXL/TTM resources when fbcon, two dumb buffers,
and primary/shadow surfaces coexist; the doctor stage rejects that route.

The fixed harness command is:

```sh
./tools/gw-vm milestone10-runtime-test --yes
```

The host requires ImageMagick's `magick` command. Libvirt may emit a PNG even
when the requested filename ends in `.ppm`; the harness therefore converts the
native capture explicitly to an 8-bit binary P6 PPM before exact comparison.

It must be preceded by the separate M9-clean gate and snapshot reset described
below. The M10 command verifies source identity, proves the default headless
build while libdrm is still absent, installs the narrow DRM dependency, builds
dual-backend and DRM-only configurations, probes the selected KMS route, runs
the three Glasswyrm services, captures screenshots, exercises VT switching,
validates restoration, and collects evidence. It exposes no arbitrary SSH
command.

```sh
./tools/gw-vm reset --yes
./tools/gw-vm milestone9-runtime-test --yes
./tools/gw-vm reset --yes
./tools/gw-vm milestone10-runtime-test --yes
```

## Physical spare-VT procedure

Use this only from an SSH session with a second recovery login available.
Replace paths and connector names with values verified by `gw_drm_probe`.

1. Build with `-Ddrm_backend=true` and run the complete host test suite.
2. Stop the getty on the spare VT and record whether it was active.
3. Capture a read-only KMS baseline:

   ```sh
   ./build-drm/tools/gw_drm_probe \
     --device /dev/dri/card0 --connector HDMI-A-1 \
     --require-mode 1024x768 --snapshot-state --output /tmp/kms-before.json
   ```

4. Start `gwm`, then `gwcomp` with `--backend drm`, then `glasswyrmd`, using
   unique socket, mirror, report, and trace paths.
5. Run only repository-owned or pinned M9 profiles. Verify the report shows the
   selected route, initial modeset, matching canonical/scanout hashes, and a
   completed page flip before treating the frame as presented.
6. Capture the physical output with an external camera or trusted capture
   device. Libvirt screenshot equality applies only to the VM route.
7. Switch away and back. Confirm release/acquire report records and that the
   last committed frame returns exactly.
8. Send `SIGTERM` to `gwcomp` over SSH. Verify the KMS baseline with
   `--expect-restored`, check KD/VT state, restart the getty only if it was
   previously active, and remove runtime sockets.

Never run this first on the only usable console.

## Required evidence

The VM run must validate, not merely collect:

- `milestone10-drm-probe.json` and KMS before/active/after snapshots;
- VT before/active/after snapshots;
- the DRM JSON-lines report and service journals;
- canonical mirror PPM and screenshots before and after VT reacquire;
- exact canonical, scanout, mirror, and screenshot hash equality;
- delayed acknowledgement and delayed release evidence;
- initial modeset, page flip, VT release/acquire, full re-modeset, and ordered
  KMS/KD/VT/master/framebuffer restoration;
- strict, sanitizer, Clang, component, historical golden, and source-layout
  results; and
- the validated binary-safe `milestone10-drm-evidence.tar` plus SHA256SUMS.

The summary is a pass only when every mandatory field is present and correct.
A screenshot that merely looks plausible is not sufficient.

The post-VT xeyes input proof requires an accepted synthetic-input
acknowledgement at root coordinates `(35, 55)`. It does not require a queued
`MotionNotify`: the accepted xeyes profile polls the current pointer through
`QueryPointer`. The resulting repaint must still produce a later correlated
page flip whose changed canonical hash matches both scanout and frame evidence.

## Forced-termination recovery

If `gwcomp` is killed before it can restore state:

1. stay connected over SSH;
2. stop any surviving Glasswyrm services;
3. switch to a known text VT with `chvt`;
4. restore a text console with the distribution's console/getty service;
5. inspect DRM master ownership and the DRM report before restarting another
   display owner; and
6. reboot only if normal VT/KMS recovery cannot restore the console.

Never assume a successful process exit after `SIGKILL`; it cannot run the
restoration path.
