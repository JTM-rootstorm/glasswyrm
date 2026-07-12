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
./tools/gw-vm milestone2-runtime-test --yes
./tools/gw-vm milestone3-runtime-test --yes
./tools/gw-vm milestone4-runtime-test --yes
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

Milestone 2 has its own fixed source/runtime scenario and leaves the accepted
Milestone 1 command and artifacts unchanged:

```sh
./tools/gw-vm milestone2-runtime-test --yes
```

It requires the tested commit to descend from
`4e219a8093c2b79857efc046c3bf0948cc7704f8`, builds in dedicated M2 strict and
sanitizer directories, and runs the M1 setup probes plus fixed little-endian,
big-endian, error-continuation, cleanup/reuse, cross-endian property, and XCB
M2 probes against `glasswyrmd-m2.service`. The scenario remains headless and
does not install Xorg, Xwayland, a desktop environment, display manager,
window manager, or compositor.

Milestone 3 validates the independently buildable and installable `libgwipc`
foundation without integrating it into a production process:

```sh
./tools/gw-vm milestone3-runtime-test --yes
```

It requires the tested commit to descend from
`d6816484f0293b8b47edf0f13e5da691014e3e7c`, runs full strict and sanitizer
tests plus an IPC-only build, installs into a staging root, and compiles C and
C++ consumers exclusively against the staged headers and pkg-config metadata.
A transient `gwipc-m3.service` then exercises handshake, ping/pong, compositor
contract round trips, memfd transfer, snapshots, structured rejections, and
malformed-client isolation over a private `SOCK_SEQPACKET` endpoint. The guest
must remain terminal-only with Xorg and Xwayland absent.

Milestone 4 has a fixed headless compositor acceptance command:

```sh
./tools/gw-vm milestone4-runtime-test --yes
```

It requires a commit descending from
`6080e094c35929d0fb2deb4b31ff4040e392a75e`. The guest performs full strict,
sanitizer, compositor-only, and IPC-only builds, then starts `gwcomp` as the
hardened transient `gwcomp-m4.service`. Repository-owned producer scenarios
exercise basic and damage frames, stacking, visibility, clipping, opacity,
buffer release, invalid metadata and buffers, malformed-peer isolation, and a
reconnecting snapshot. Exact hashes are checked against repository fixtures.
No arbitrary scenario names are accepted.

## Safety

Snapshot reset, emerge, unmerge, and the complete packaging test change guest
state. The Milestone 1, Milestone 2, and Milestone 3 runtime tests can also
install missing
fixed dependencies. These operations require `--yes` unless the relevant
safety setting explicitly allows them. Shutdown requests a graceful guest
shutdown; the harness does not destroy a running domain by default.

Arbitrary SSH is outside the interface. The first-pass harness provides no
general remote-command or interactive SSH command.

The harness never replaces system Xorg automatically. General lifecycle and
packaging operations do not require systemd, but the milestone runtime tests
require it for their transient acceptance units.

## Reports

Generated logs and JSON reports are written beneath the configured artifact
directory, `artifacts/vm/latest` by default. The artifact contents are ignored;
only `artifacts/vm/.gitkeep` is tracked. `collect` records Portage and profile
diagnostics alongside a machine-readable `summary.json`.

Milestone 1 acceptance writes `milestone1-runtime-test.log`,
`milestone1-meson-test.log`, `milestone1-xcb-probe.log`,
`milestone1-journal.log`, and `milestone1-summary.json` beneath the configured
artifact directory.

Milestone 2 acceptance writes `milestone2-runtime-test.log`,
`milestone2-meson-test.log`, `milestone2-raw-probe.log`,
`milestone2-xcb-probe.log`, `milestone2-journal.log`,
`milestone2-facts.env`, and `milestone2-summary.json`. The summary can pass only
when its collected evidence proves the required base, toolchain, X server
absence, M1 regression, M2 tests, sanitizer status, every fixed raw and XCB
probe, systemd shutdown, and current-invocation journal gates.

Milestone 3 acceptance writes `milestone3-runtime-test.log`,
`milestone3-meson-test.log`, `milestone3-install-test.log`,
`milestone3-handshake.log`, `milestone3-fd-transfer.log`,
`milestone3-snapshot.log`, `milestone3-malformed.log`,
`milestone3-journal.log`, `milestone3-facts.env`, and
`milestone3-summary.json`. Its summary validates the exact tested commit,
toolchain and version domains, build and staged-install gates, every fixed
process probe, service shutdown, socket cleanup, and the current invocation
journal.

Milestone 4 acceptance writes `milestone4-runtime-test.log`,
`milestone4-meson-test.log`, `milestone4-producer.log`,
`milestone4-golden-test.log`, `milestone4-buffer-release.log`,
`milestone4-malformed.log`, `milestone4-journal.log`,
`milestone4-facts.env`, `milestone4-summary.json`, and the binary-safe
`milestone4-frames.tar`. A passing summary requires exact source identity,
build and runtime evidence, at least one golden hash, socket cleanup, a valid
frame archive, and the current systemd invocation journal.
