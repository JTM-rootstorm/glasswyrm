#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
gw_vm=$repo_root/tools/gw-vm
library=$repo_root/tools/gw-vm.d/lib/milestone14.sh
wrapper=$repo_root/tools/gw-vm.d/scenarios/milestone14-runtime-test.sh
temporary=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m14-vm-cli.XXXXXX")
trap 'rm -rf "$temporary"' EXIT

fail() { printf 'm14 VM CLI contract: %s\n' "$*" >&2; exit 1; }
require_text() {
  local file=$1 expected=$2
  grep -F -- "$expected" "$file" >/dev/null || fail "$file lacks: $expected"
}

"$gw_vm" help >"$temporary/help"
require_text "$temporary/help" 'milestone14-runtime-test --yes'

if "$gw_vm" milestone14-runtime-test >"$temporary/gate" 2>&1; then
  fail 'direct M14 command bypassed --yes'
fi
require_text "$temporary/gate" "Action 'milestone14-runtime-test' requires --yes"
if "$wrapper" >"$temporary/wrapper-gate" 2>&1; then
  fail 'M14 scenario wrapper bypassed --yes'
fi
require_text "$temporary/wrapper-gate" "Action 'milestone14-runtime-test' requires --yes"
if "$gw_vm" scenario milestone14-runtime-test >"$temporary/scenario-gate" 2>&1; then
  fail 'named M14 scenario bypassed --yes'
fi
require_text "$temporary/scenario-gate" \
  "Action 'milestone14-runtime-test' requires --yes"
if "$gw_vm" scenario 'milestone14-runtime-test;id' >"$temporary/injection" 2>&1; then
  fail 'scenario-name injection was accepted'
fi
require_text "$temporary/injection" 'Scenario names are fixed'

guest=$(bash -c 'source "$1"; milestone14_guest_script' _ "$library")
cat "$library" >"$temporary/contract"
printf '%s\n' "$guest" >>"$temporary/contract"

for expected in \
  6864ea631d61636289a21c7d2d6655a17be0c004 \
  "snapshot name 'base'" 'snapshot_name=base' \
  'reset; milestone13-runtime-test; reset; milestone14-runtime-test' \
  /var/tmp/glasswyrm-build-m14 /var/tmp/glasswyrm-build-m14-asan \
  /var/tmp/glasswyrm-build-m14-default /var/tmp/glasswyrm-build-m14-headless \
  /var/tmp/glasswyrm-build-m14-drm /var/tmp/glasswyrm-build-m14-clang \
  /var/tmp/glasswyrm-build-m14-components /var/tmp/glasswyrm-m14-headless \
  /var/tmp/glasswyrm-m14-qxl /var/tmp/glasswyrm-m14-control \
  /var/tmp/glasswyrm-m14-clients \
  /var/tmp/glasswyrm-m14-artifacts /run/glasswyrm-m14 \
  '-Dexperimental=false' '-Dexperimental=true' '-Drender_gl=false' \
  '-Drender_gl=true' '-Dasan=true' '-Dubsan=true' \
  server-historical server-m14 compositor-headless-vrr 'setup_build "$drm"' \
  compositor-software compositor-gles api_consumer_versions=0.1-0.9 \
  gwipc_staged_consumers_test.sh source_layout_test.sh \
  drm-vrr-capability-property drm-vrr-saved-state drm-vrr-timing \
  drm-presenter-vrr-state drm-presenter-vrr-integration \
  headless-vrr-simulation headless-vrr-presenter gwcomp-vrr-presentation \
  gwm-vrr-process server-vrr-lifecycle output-tools-control-client \
  gw-vrr-dispatch gw-vrr-integration '"little":true,"big":true' \
  '--headless-output LEFT:800x600@60000' \
  '--headless-output RIGHT:640x480@75000' \
  '--headless-vrr LEFT=40000-60000' '--headless-vrr RIGHT=48000-75000' \
  '--vrr-report' '--vrr-protocol' '--vrr "$mode"' 'gwinfo" --socket' \
  'm14_vrr_client' off fullscreen focused app-requested always-eligible borderless \
  '--preference "$preference"' 'm12/acquire_sdl.sh' 'm12/build_clients.sh' \
  5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 \
  'systemctl restart gwm-m14-headless.service' \
  'systemctl restart gwcomp-m14-headless.service' \
  'M14 QXL unsupported profile requires driver qxl' \
  'gwout: selected output does not provide controllable VRR' \
  '--backend drm' '--drm-api auto' '--connector "$connector"' \
  '--mode 1024x768' '--property="TTYPath=$target_vt"' \
  '--property=KillMode=mixed' '--property=SuccessExitStatus=143' \
  'chvt 1' 'chvt "${target_vt#/dev/tty}"' 'vrr-restore' \
  'milestone14-qxl-capability.json' 'milestone14-qxl-state.json' \
  'milestone14-headless-report.jsonl' 'milestone14-gw-vrr.log' \
  'milestone14-gwout.log' 'milestone14-gwinfo.json' \
  'milestone14-policy-matrix.json' 'milestone14-sdl-vrr.json' \
  'milestone14-sdl-probe.json' 'milestone14-client-build.log' \
  'milestone14-restart.json' 'milestone14-restoration.json' \
  'milestone14-glasswyrmd-journal.log' 'milestone14-gwm-journal.log' \
  'milestone14-gwcomp-journal.log' 'milestone14-facts.env' \
  'milestone14-summary.json' 'milestone14-vm-vrr-evidence.tar' \
  'sha256sum -- * >SHA256SUMS' validate_vm_evidence.py \
  'PYTHONDONTWRITEBYTECODE=1 "${command[@]}"'; do
  require_text "$temporary/contract" "$expected"
done

[[ -s $repo_root/tests/compat/m14/vm_evidence.schema.json ]] ||
  fail 'M14 VM evidence schema is missing'

(
  unset GW_VM_MILESTONE14_LOADED
  # shellcheck source=/dev/null
  source "$library"
  milestone14_source_status_ignored \
    '?? tests/compat/m14/__pycache__/validate_vm_evidence.cpython-314.pyc' ||
    fail 'Python bytecode cache should not invalidate M14 source identity'
  milestone14_source_status_ignored \
    '?? tools/__pycache__/gw-hw.cpython-314.pyc' ||
    fail 'top-level tool bytecode should not invalidate M14 source identity'
  if milestone14_source_status_ignored '?? tests/compat/m14/random_probe.cpp'; then
    fail 'untracked implementation source must invalidate M14 source identity'
  fi
)

if grep -Eq '(^|[[:space:]])eval([[:space:]]|$)' "$library"; then
  fail 'M14 harness contains eval'
fi

printf '%s\n' 'm14 VM CLI contract: passed'
