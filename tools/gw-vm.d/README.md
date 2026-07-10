# Glasswyrm VM Validation Harness

`tools/gw-vm` is the repository-owned entrypoint for validating the Gentoo
overlay in a fresh libvirt guest. It transfers the overlay, registers it as an
independent Portage repository, runs named packaging scenarios, and collects
the resulting reports under `artifacts/vm/`.

The harness validates Portage dependency resolution and package behavior. A
copy of locally built runtime files is not a substitute for this workflow.

## Configuration

Copy `tools/gw-vm.d/config.example.toml` to the ignored local path
`tools/gw-vm.d/config.toml`, then set `[libvirt].domain` and the guest SSH
details. The harness resolves its configuration in this order:

1. `--config <path>`
2. `GLASSWYRM_VM_CONFIG`
3. `tools/gw-vm.d/config.toml`
4. `tools/gw-vm.d/config.example.toml`

The following environment variables override values from the selected file:

```text
GLASSWYRM_VM_NAME
GLASSWYRM_VM_SSH_HOST
GLASSWYRM_VM_SSH_USER
GLASSWYRM_VM_SSH_PORT
GLASSWYRM_VM_LIBVIRT_URI
GLASSWYRM_VM_SNAPSHOT
GLASSWYRM_VM_OVERLAY_PATH
GLASSWYRM_VM_ARTIFACTS_PATH
GLASSWYRM_VM_SOURCE_PATH
```

`GLASSWYRM_VM_NAME` overrides `[libvirt].domain`. Commands that manage a guest
lifecycle require a configured domain. The default configuration intentionally
leaves it empty so a repository checkout cannot target a VM accidentally.

Paths in `[paths]` are host paths relative to the repository root. The
`shared_overlay_path` and `shared_artifacts_path` values are guest paths.
`paths.source` must remain inside the repository, and `shared_source_path` is
the fixed guest destination used by Milestone 1 source validation.

## Host Requirements

The host needs Bash, OpenSSH (`ssh`), `rsync`, `virsh`, and Python 3 for JSON
report generation. Run the preflight check before a packaging test:

```sh
./tools/gw-vm doctor
./tools/gw-vm status
```

The guest must be reachable over SSH as a user able to manage Portage and its
configuration; the first pass assumes the configured user is `root`. Snapshot
reset also requires the configured libvirt snapshot to exist.
`app-portage/portage-utils` is optional; when `qlist` is unavailable, collection
records that fact without failing the packaging scenario.

## Commands

The public interface is deliberately limited to named lifecycle and packaging
actions:

```sh
./tools/gw-vm help
./tools/gw-vm doctor
./tools/gw-vm status
./tools/gw-vm boot
./tools/gw-vm shutdown
./tools/gw-vm reset --yes
./tools/gw-vm push-overlay
./tools/gw-vm push-source
./tools/gw-vm register-overlay
./tools/gw-vm metadata
./tools/gw-vm pretend x11-base/glasswyrm
./tools/gw-vm emerge x11-base/glasswyrm --yes
./tools/gw-vm unmerge x11-wm/gwm --yes
./tools/gw-vm narrow-test gwm
./tools/gw-vm collect
./tools/gw-vm full-packaging-test --yes
./tools/gw-vm milestone1-runtime-test --yes
```

`push-overlay` synchronizes `packaging/gentoo/overlay/` to the configured guest
overlay path. `register-overlay` writes a dedicated `repos.conf` entry; it does
not copy files into the main Gentoo repository.

Before using `rsync --delete`, the harness requires its ownership marker in a
non-empty guest destination. This prevents a mistaken path from deleting an
unrelated directory.

`push-source` similarly requires `.glasswyrm-vm-source` and excludes `.git/`,
`Plans/`, artifacts, host build directories, and the ignored local VM config.
It refuses a non-empty destination that lacks the ownership marker.

`narrow-test gwm` checks the Portage pretend output for unexpected rebuilds of
`x11-base/glasswyrmd` and `x11-base/gwcomp`. Use `--allow-abi-rebuild` only when
an intentional shared-library or IPC ABI change requires those rebuilds.

## Canonical Validation

The complete fresh-VM workflow is:

```sh
./tools/gw-vm doctor
./tools/gw-vm full-packaging-test --yes
```

The full scenario resets the configured snapshot, boots the guest, delivers and
registers the overlay, refreshes Portage metadata, installs the Glasswyrm
package set, exercises the narrow `gwm` install and removal path, and collects
diagnostics. It attempts collection even when an earlier validation step fails.

Until the package directories contain ebuilds, overlay registration and metadata
validation can be exercised, but package pretend and emerge scenarios are
expected to fail because there is nothing for Portage to resolve.

The canonical Milestone 1 source/runtime workflow is separate from packaging:

```sh
./tools/gw-vm milestone1-runtime-test --yes
```

It boots the configured guest, safely synchronizes the source tree, installs
only the fixed build/test dependency set through Portage, runs strict and
sanitizer Meson tests, and exercises `glasswyrmd` under a transient systemd unit
with raw and XCB setup probes. It does not install Xorg, Xwayland, a desktop
environment, display manager, compositor, or window manager.

## Safety

Snapshot reset, emerge, unmerge, and the complete packaging test change guest
state. The Milestone 1 runtime test can also install missing fixed dependencies.
These operations require `--yes` unless the relevant safety setting explicitly
allows them. Shutdown requests a graceful guest shutdown; the harness does not
destroy a running domain by default.

Arbitrary SSH is outside the interface. The first-pass harness provides no
general remote-command or interactive SSH command.

The harness never replaces system Xorg automatically and does not require
systemd in the guest.

## Reports

Generated logs and JSON reports are written beneath the configured artifact
directory, `artifacts/vm/latest` by default. The artifact contents are ignored;
only `artifacts/vm/.gitkeep` is tracked. `collect` records Portage and profile
diagnostics alongside a machine-readable `summary.json`.

Milestone 1 acceptance writes `milestone1-runtime-test.log`,
`milestone1-meson-test.log`, `milestone1-xcb-probe.log`,
`milestone1-journal.log`, and `milestone1-summary.json` beneath the configured
artifact directory.
