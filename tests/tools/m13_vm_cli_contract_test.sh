#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
gw_vm=$repo_root/tools/gw-vm
library=$repo_root/tools/gw-vm.d/lib/milestone13.sh
wrapper=$repo_root/tools/gw-vm.d/scenarios/milestone13-runtime-test.sh
temporary=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m13-vm-cli.XXXXXX")
trap 'rm -rf "$temporary"' EXIT

fail() { printf 'm13 VM CLI contract: %s\n' "$*" >&2; exit 1; }
require_text() {
  local file=$1 expected=$2
  grep -F -- "$expected" "$file" >/dev/null || fail "$file lacks: $expected"
}

"$gw_vm" help >"$temporary/help"
require_text "$temporary/help" 'milestone13-runtime-test --yes'

if "$gw_vm" milestone13-runtime-test >"$temporary/gate" 2>&1; then
  fail 'direct M13 command bypassed --yes'
fi
require_text "$temporary/gate" "Action 'milestone13-runtime-test' requires --yes"
if "$wrapper" >"$temporary/wrapper-gate" 2>&1; then
  fail 'M13 scenario wrapper bypassed --yes'
fi
require_text "$temporary/wrapper-gate" "Action 'milestone13-runtime-test' requires --yes"
if "$gw_vm" scenario 'milestone13-runtime-test;id' >"$temporary/injection" 2>&1; then
  fail 'scenario-name injection was accepted'
fi
require_text "$temporary/injection" 'Scenario names are fixed'

guest=$(bash -c 'source "$1"; milestone13_guest_script' _ "$library")
cat "$library" >"$temporary/guest"
printf '%s\n' "$guest" >>"$temporary/guest"

for expected in \
  d3440d3b8df1533410a9a2c4be46f2eea0cfb88d \
  /var/tmp/glasswyrm-build-m13 /var/tmp/glasswyrm-build-m13-asan \
  /var/tmp/glasswyrm-build-m13-software /var/tmp/glasswyrm-build-m13-gles \
  /var/tmp/glasswyrm-build-m13-default /var/tmp/glasswyrm-build-m13-tools \
  /var/tmp/glasswyrm-m13-headless /var/tmp/glasswyrm-m13-drm \
  /var/tmp/glasswyrm-m13-control /var/tmp/glasswyrm-m13-scenes \
  /var/tmp/glasswyrm-m13-control-data /var/tmp/glasswyrm-m13-artifacts \
  'runtime=/run/glasswyrm-m13' 'gwm.sock' 'gwcomp.sock' 'control.sock' \
  /tmp/.X11-unix/X99 \
  '-Dexperimental=false' '-Dexperimental=true' '-Drender_gl=false' \
  '-Drender_gl=true' '-Dasan=true' '-Dubsan=true' \
  source_layout_test.sh gwipc_staged_consumers_test.sh \
  '--backend headless' '--headless-output LEFT:640x480@60000' \
  '--headless-output RIGHT:800x600@60000' '--output-model' \
  '--control-socket' '--scale-protocol' '--game-compat' \
  'gwinfo" --socket' 'gwout" --socket' '--position 640,0' \
  m13_raw_output_probe.py m13_scale_client 'systemctl restart' \
  m13_sdl_display_probe.c 'SDL2-2.32.10.tar.gz' \
  '--synthetic-input-socket' 'gwinput_m8' '--scenario crossing' \
  'scene_line_before=$(wc -l <"$scenes/scene.jsonl")' \
  '--disable' '--enable' validate_frame_sets.py \
  '--backend drm' '--scale 4/3' '--transform rotate-180' \
  '--setenv="PATH=$software/src:/usr/bin:/bin"' \
  '--property="DeviceAllow=$keyboard r"' '--property="DeviceAllow=$pointer r"' \
  '--property=StandardOutput=journal' '--property=StandardError=journal' \
  '--property=KillMode=mixed' '--property=SuccessExitStatus=143' \
  '--scene-manifest "$control_data/drm-scene.jsonl"' \
  'drm-screen-ready' 'screen-captured' '--scale 1/1' '--transform normal' \
  milestone13-output-scaling-evidence.tar milestone13-facts.env; do
  require_text "$temporary/guest" "$expected"
done

for artifact in milestone13-runtime-test.log milestone13-meson-test.log \
  milestone13-source-layout.log milestone13-output-inventory.json \
  milestone13-layout-before.json milestone13-layout-after.json \
  milestone13-gwinfo-outputs.json milestone13-gwinfo-windows.json \
  milestone13-gwout.log milestone13-randr.log milestone13-gw-scale.log \
  milestone13-scale-client.json milestone13-renderer-software.jsonl \
  milestone13-pointer-crossing.json milestone13-sdl-displays.json \
  milestone13-fullscreen-outputs.json \
  milestone13-frame-sets.jsonl \
  milestone13-renderer-gles.jsonl milestone13-drm-report.jsonl \
  milestone13-renderer-drm.jsonl milestone13-drm-representation.json \
  milestone13-glasswyrmd-journal.log milestone13-gwm-journal.log \
  milestone13-gwcomp-journal.log milestone13-facts.env \
  milestone13-headless-outputs.tar milestone13-drm-canonical.ppm \
  milestone13-drm-screen.ppm milestone13-output-scaling-evidence.tar; do
  require_text "$library" "$artifact"
done

require_text "$library" 'reset; milestone12-runtime-test; reset; milestone13-runtime-test'
require_text "$library" '[[ $name == milestone13-runtime-test.log ]] && continue'
require_text "$library" "record.get('schema')=='glasswyrm-scene-v2'"
require_text "$library" "record.get('cursors',[])"
require_text "$repo_root/tests/integration/gwinput_m8.cpp" \
  '--hold-until PATH'
require_text "$repo_root/tests/integration/gwinput_m8.cpp" \
  '--step-directory PATH'
require_text "$library" '--hold-until "$crossing_release"'
require_text "$library" '--step-directory "$crossing_steps"'
require_text "$library" 'synthetic_pid=$!'
require_text "$library" 'compare_software_pid=0'
require_text "$library" '"$compare_software_pid"'
require_text "$library" '[[ -S $runtime/control.sock && -S /tmp/.X11-unix/X99 ]]'
require_text "$library" "\$line == '?? tests/'*'/__pycache__/'*.pyc"
require_text "$library" 'PYTHONDONTWRITEBYTECODE=1 "${command[@]}"'
require_text "$library" '--scenario pointer-anchor --result-json "$control_data/post-vt-pointer-anchor.json"'
require_text "$library" "assert any(state!=baseline for state in states)"
require_text "$library" 'output=int(output,16) if isinstance(output,str) else output'
require_text "$library" 'STOP_MAIN_IDENTITY_GONE'
require_text "$library" 'STOP_EVENTFD_COUNT_BEFORE'
require_text "$library" 'drm_live_eventfd_count=$(unit_eventfd_count glasswyrm-m13-drm.service)'
require_text "$library" 'event_fd_closed=eventfd_after==0 and cgroup_empty and process_after==0'
require_text "$library" "full_copy_reason')=='output-configuration-changed'"
require_text "$library" "{canonical_ordinal,post_vt_ordinal} <= flip_ordinals & frame_ordinals"
require_text "$library" '[[ $logind_socket_enabled_before != masked-runtime ]]'
require_text "$library" 'printf "screen-capture-failed\n" >"$1"'
require_text "$library" 'sha256sum -- * >SHA256SUMS'
require_text "$library" 'legacy_command configure 480 80 320 240'
require_text "$library" 'wait_outputs_match()'
require_text "$library" '"$control_data/legacy-spanning-restored-left.ppm"'
require_text "$library" '"$control_data/legacy-spanning-restored-right.ppm"'
require_text "$library" 'cmp -s "$left_expected" "$left_target"'
require_text "$library" 'cmp -s "$right_expected" "$right_target"'
require_text "$library" '"$control_data/milestone13-legacy-spanning-left.ppm"'
require_text "$library" '"$control_data/milestone13-legacy-spanning-right.ppm"'
require_text "$library" '"$control_data/milestone13-flipped.ppm"'
require_text "$library" '"$control_data/restart-left-before.ppm"'
require_text "$library" '"$control_data/restart-right-before.ppm"'

(
  unset GW_VM_MILESTONE13_LOADED
  # shellcheck source=/dev/null
  source "$library"
  milestone13_source_status_ignored \
    '?? tests/compat/m13/__pycache__/validate_frame_sets.cpython-314.pyc' ||
    fail 'Python bytecode cache should not invalidate M13 source identity'
  if milestone13_source_status_ignored '?? tests/compat/m13/random_probe.cpp'; then
    fail 'untracked implementation source must invalidate M13 source identity'
  fi
)
require_text "$library" 'wait_cursor_position "$left_id" 100 100'
require_text "$library" 'wait_cursor_position "$right_id" 700 479'
require_text "$library" ': >"$crossing_release"'
compositor_stop_line=$(grep -n -m1 'stop_transient_unit gwcomp-m13.service' \
  "$library" | cut -d: -f1)
report_rotate_line=$(grep -n -m1 \
  'milestone13-renderer-software-pre-restart.jsonl' "$library" | cut -d: -f1)
compositor_start_line=$(grep -n -m1 \
  'start_headless_compositor "$software"' "$library" | cut -d: -f1)
((compositor_stop_line < report_rotate_line &&
  report_rotate_line < compositor_start_line)) ||
  fail 'compositor report is not rotated between stop and start'
require_text "$library" \
  '>"$control_data/milestone13-renderer-software.jsonl"'
require_text "$library" '((service_checks == 12))'
legacy_line=$(grep -n -m1 'm13_legacy_client.py' "$library" | cut -d: -f1)
crossing_line=$(grep -n -m1 'scene_line_before=$(wc -l' "$library" | cut -d: -f1)
((legacy_line < crossing_line)) ||
  fail 'pointer crossing runs before the first legacy surface is ready'
if grep -Eq '(^|[[:space:]])eval([[:space:]]|$)' "$library"; then
  fail 'M13 harness contains eval'
fi

printf '%s\n' 'm13 VM CLI contract: passed'
