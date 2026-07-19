#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE13_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE13_LOADED=1

M13_REQUIRED_BASE_COMMIT=d3440d3b8df1533410a9a2c4be46f2eea0cfb88d
M13_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m13-artifacts
M13_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m13-control
M13_SCREENSHOT_WAIT_SECONDS=1800
M13_GUEST_EXITED_BEFORE_SCREENSHOT=2
M13_TESTED_COMMIT=
M13_TEXT_ARTIFACTS=(
  milestone13-runtime-test.log milestone13-meson-test.log
  milestone13-sdl-build.log
  milestone13-source-layout.log milestone13-output-inventory.json
  milestone13-layout-before.json milestone13-layout-after.json
  milestone13-gwinfo-outputs.json milestone13-gwinfo-windows.json
  milestone13-gwout.log milestone13-gwout-result.json
  milestone13-randr.log milestone13-randr-little.json
  milestone13-randr-big.json milestone13-gw-scale.log
  milestone13-gw-scale-little.json milestone13-gw-scale-big.json
  milestone13-scale-client.json milestone13-frame-sets.jsonl
  milestone13-pointer-crossing.json milestone13-sdl-displays.json
  milestone13-fullscreen-outputs.json
  milestone13-renderer-software.jsonl milestone13-renderer-gles.jsonl
  milestone13-renderer-drm.jsonl
  milestone13-renderer-fractional-diff.json milestone13-drm-report.jsonl
  milestone13-drm-representation.json
  milestone13-kms-before.json milestone13-kms-after.json
  milestone13-vt-before.json milestone13-vt-after.json
  milestone13-restart.json milestone13-restoration.json
  milestone13-getty-state.json milestone13-logind-state.json
  milestone13-glasswyrmd-journal.log milestone13-gwm-journal.log
  milestone13-gwcomp-journal.log milestone13-session-journal.log
  milestone13-facts.env
)
M13_BINARY_ARTIFACTS=(
  milestone13-headless-outputs.tar milestone13-drm-canonical.ppm
  milestone13-drm-screen.ppm milestone13-output-scaling-evidence.tar
)

milestone13_guest_prerequisite_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
[[ -c /dev/uinput && -c /dev/tty1 && -c /dev/tty2 ]] || {
  printf '%s\n' 'M13 requires /dev/uinput and two usable virtual terminals.' >&2
  exit 30
}
shopt -s nullglob
cards=(/dev/dri/card[0-9]*)
((${#cards[@]})) || { printf '%s\n' 'M13 requires a DRM primary node.' >&2; exit 31; }
primary=${cards[0]}; card=${primary##*/}; connector=
for status in /sys/class/drm/"$card"-*/status; do
  [[ $(<"$status") == connected ]] || continue
  grep -Fxq 1024x768 "${status%/status}/modes" || continue
  connector=${status%/status}; connector=${connector##*/}; connector=${connector#"$card"-}
  break
done
[[ -n $connector ]] || { printf '%s\n' 'M13 requires a connected exact 1024x768 output.' >&2; exit 32; }
printf 'drm_primary_node=%s\ndrm_connector=%s\ndrm_mode=1024x768\n' "$primary" "$connector"
printf 'target_vt=/dev/tty2\nuinput_device=/dev/uinput\n'
GUEST_SCRIPT
}

verify_milestone13_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line || $line == '?? Plans/'* || $line == '?? .codex/'* ]] ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || {
    printf 'Milestone 13 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2
    return 1
  }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  [[ -z $M13_TESTED_COMMIT || $current == "$M13_TESTED_COMMIT" ]]
}

prepare_milestone13_evidence() {
  M13_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_milestone13_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M13_REQUIRED_BASE_COMMIT" "$M13_TESTED_COMMIT" || {
    printf 'HEAD is not based on required Milestone 13 commit %s\n' \
      "$M13_REQUIRED_BASE_COMMIT" >&2
    return 1
  }
}

milestone13_guest_script() {
  cat <<'GUEST_SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
source_dir=$1 artifact_dir=$2 drm_device=$3 connector=$4 target_vt=$5 tested_commit=$6
build=/var/tmp/glasswyrm-build-m13
asan=/var/tmp/glasswyrm-build-m13-asan
software=/var/tmp/glasswyrm-build-m13-software
gles=/var/tmp/glasswyrm-build-m13-gles
default=/var/tmp/glasswyrm-build-m13-default
tools_build=/var/tmp/glasswyrm-build-m13-tools
clients=/var/tmp/glasswyrm-m13-clients
headless=/var/tmp/glasswyrm-m13-headless
drm=/var/tmp/glasswyrm-m13-drm
control=/var/tmp/glasswyrm-m13-control
scenes=/var/tmp/glasswyrm-m13-scenes
control_data=/var/tmp/glasswyrm-m13-control-data
runtime=/run/glasswyrm-m13
facts=$artifact_dir/milestone13-facts.env
failure_stage=prerequisite scenario_exit=1
declare -A result
required_results=(historical_default strict_software strict_gles sanitizer
  component_builds source_layout api_consumers m1_m12_regressions
  output_inventory stable_id_replay logical_physical_geometry integer_scaling
  output_enable_disable pointer_output_crossing sdl_display_discovery
  fullscreen_outputs
  fractional_scaling transforms surface_membership primary_transition
  legacy_fallback scaled_pixmap gw_scale_events multi_output_randr gwinfo_text
  gwinfo_json gwout_commit stale_rejection busy_rejection headless_frame_hashes
  aggregate_hash renderer_comparison gwm_replay compositor_replay
  drm_scale_transform screenshot_equality vt_replay restoration service_results
  socket_cleanup archive_validation journal_evidence)
for key in "${required_results[@]}"; do result[$key]=failed; done
clang=unavailable layout_generation=unknown output_ids=unknown
headless_aggregate_hash=unknown drm_mode=1024x768
service_checks=0
randr_pid=0 legacy_pid=0 scale_pid=0 synthetic_pid=0
gles_legacy_pid=0 drm_legacy_pid=0
getty_state_captured=false logind_state_captured=false original_vt=
getty_unit='' getty_active_before='' getty_enabled_before=''
logind_unit=systemd-logind.service logind_socket=systemd-logind-varlink.socket
logind_active_before='' logind_socket_active_before=''
logind_enabled_before='' logind_socket_enabled_before=''

write_facts() {
  {
    printf 'required_base_commit=d3440d3b8df1533410a9a2c4be46f2eea0cfb88d\n'
    printf 'tested_commit=%s\nfailure_stage=%s\nscenario_exit=%s\n' \
      "$tested_commit" "$failure_stage" "$scenario_exit"
    printf 'api_version=0.8.0\nsoversion=0\nwire_version=1.0\n'
    printf 'api_consumer_versions=0.1-0.8\nsource_layout_allowlist=empty\n'
    printf 'compiler_c=%s\ncompiler_cxx=%s\n' \
      "$(cc --version 2>/dev/null | head -n1 || echo unavailable)" \
      "$(c++ --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' \
      "$(meson --version 2>/dev/null || echo unavailable)" \
      "$(ninja --version 2>/dev/null || echo unavailable)" \
      "$(systemctl --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'output_descriptor_count=2\nstable_output_ids=true\n'
    printf 'headless_output_names=LEFT,RIGHT\nprimary_output=LEFT\n'
    printf 'root_logical_geometry=1280x480\ncontrol_socket_mode=0600\n'
    printf 'layout_generation=%s\noutput_ids=%s\nheadless_aggregate_hash=%s\n' \
      "$layout_generation" "$output_ids" "$headless_aggregate_hash"
    printf 'drm_connector=%s\ndrm_mode=%s\nclang=%s\n' \
      "$connector" "$drm_mode" "$clang"
    printf 'x_servers_absent=true\ndisplay_manager_absent=true\n'
    for key in "${required_results[@]}"; do printf '%s=%s\n' "$key" "${result[$key]}"; done
  } >"$facts"
}

cleanup() {
  local saved_status=$?
  set +e
  local pid
  for pid in "$randr_pid" "$legacy_pid" "$scale_pid" "$synthetic_pid" \
    "$gles_legacy_pid" "$drm_legacy_pid"; do
    if ((pid > 0)); then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  done
  systemctl stop glasswyrmd-m13.service gwm-m13.service gwcomp-m13.service \
    glasswyrmd-m13-compare.service gwm-m13-compare.service \
    gwcomp-m13-compare.service \
    glasswyrmd-m13-gles.service gwm-m13-gles.service gwcomp-m13-gles.service \
    glasswyrm-m13-drm.service \
    gw-uinput-m13.service >/dev/null 2>&1 || true
  if [[ $logind_state_captured == true ]]; then
    systemctl unmask --runtime "$logind_unit" "$logind_socket" >/dev/null 2>&1
    [[ $logind_socket_active_before != active ]] || \
      systemctl start "$logind_socket" >/dev/null 2>&1
    [[ $logind_active_before != active ]] || \
      systemctl start "$logind_unit" >/dev/null 2>&1
    [[ $logind_socket_active_before == active ]] || \
      systemctl stop "$logind_socket" >/dev/null 2>&1
    [[ $logind_active_before == active ]] || \
      systemctl stop "$logind_unit" >/dev/null 2>&1
  fi
  if [[ $getty_state_captured == true ]]; then
    [[ $getty_active_before != active ]] || \
      systemctl start "$getty_unit" >/dev/null 2>&1
    [[ $getty_active_before == active ]] || \
      systemctl stop "$getty_unit" >/dev/null 2>&1
  fi
  [[ -z $original_vt ]] || chvt "$original_vt" >/dev/null 2>&1
  rm -f /tmp/.X11-unix/X99 "$runtime"/*.sock
  write_facts
  return "$saved_status"
}
trap cleanup EXIT

rm -rf -- "$build" "$asan" "$software" "$gles" "$default" "$tools_build" \
  "$clients" "$headless" "$drm" "$control" "$scenes" "$control_data" \
  "$artifact_dir" "$runtime"
install -d -m 0755 "$artifact_dir" "$headless" "$drm" "$control" "$scenes" "$control_data"
install -d -m 0700 "$runtime"

failure_stage=dependencies
install -d -m 0755 /etc/portage/package.use
printf 'media-libs/libglvnd X\nmedia-libs/mesa -llvm\n' \
  >/etc/portage/package.use/glasswyrm-m13
emerge --oneshot --noreplace dev-build/meson dev-build/ninja dev-build/cmake \
  dev-vcs/git net-misc/curl app-crypt/gnupg app-misc/jq \
  media-libs/mesa x11-libs/libdrm dev-libs/libinput x11-libs/libxkbcommon \
  x11-misc/xkeyboard-config \
  x11-libs/libX11 x11-libs/libXext x11-libs/libXfixes x11-libs/libXdamage \
  x11-libs/libXrender x11-libs/libXcomposite x11-libs/libXrandr \
  x11-libs/libxcb x11-libs/xcb-util x11-base/xorg-proto
for forbidden in x11-base/xorg-server x11-base/xwayland x11-base/xwayland-run \
  x11-misc/xvfb x11-misc/lightdm x11-misc/sddm gnome-base/gdm \
  gui-apps/greetd; do
  if qlist -IC "$forbidden" 2>/dev/null | grep -q .; then
    printf 'Forbidden M13 package installed: %s\n' "$forbidden" >&2
    exit 1
  fi
done
if systemctl is-active --quiet display-manager.service; then
  printf '%s\n' 'A display manager is active during M13 acceptance.' >&2
  exit 1
fi
[[ ! -e /etc/systemd/system/display-manager.service &&
   ! -L /etc/systemd/system/display-manager.service &&
   ! -e /usr/lib/systemd/system/display-manager.service &&
   ! -L /usr/lib/systemd/system/display-manager.service ]]

failure_stage=sdl-acquisition
"$source_dir/tests/compat/m12/acquire_sdl.sh" "$clients/download"
sdl_archive=$clients/download/SDL2-2.32.10.tar.gz
[[ $(sha256sum "$sdl_archive" | awk '{print $1}') == \
   5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 ]]
failure_stage=sdl-build
"$source_dir/tests/compat/m12/build_clients.sh" "$sdl_archive" \
  "$clients/source" "$clients/build" "$clients/install" \
  >"$artifact_dir/milestone13-sdl-build.log" 2>&1
read -r -a sdl_flags < <(
  PKG_CONFIG_PATH="$clients/install/lib64/pkgconfig:$clients/install/lib/pkgconfig" \
    pkg-config --cflags --libs sdl2
)
cc -std=c11 -Wall -Wextra -Werror \
  "$source_dir/tests/compat/m13/m13_sdl_display_probe.c" \
  -o "$clients/install/bin/m13_sdl_display_probe" \
  "${sdl_flags[@]}"

failure_stage='build-matrix'
setup_build() { local directory=$1; shift; meson setup "$directory" "$source_dir" --wipe -Dwerror=true "$@"; meson compile -C "$directory"; }
setup_build "$default" -Dexperimental=false -Drender_gl=false
meson test -C "$default" --print-errorlogs
result[historical_default]=passed
setup_build "$software" -Dexperimental=true -Drender_gl=false \
  -Dheadless_backend=true -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$software" --print-errorlogs | tee "$artifact_dir/milestone13-meson-test.log"
result[strict_software]=passed
setup_build "$build" -Dexperimental=true -Drender_gl=false \
  -Dheadless_backend=true -Ddrm_backend=true -Dlibinput_backend=true
setup_build "$gles" -Dexperimental=true -Drender_gl=true \
  -Dheadless_backend=true -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$gles" --print-errorlogs
result[strict_gles]=passed
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Dasan=true -Dubsan=true \
  -Dexperimental=true -Drender_gl=true -Dheadless_backend=true \
  -Ddrm_backend=true -Dlibinput_backend=true
meson compile -C "$asan" -j1
meson test -C "$asan" --print-errorlogs
result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ setup_build "${build}-clang" -Dexperimental=true \
    -Drender_gl=true -Dheadless_backend=true -Ddrm_backend=true \
    -Dlibinput_backend=true
  meson test -C "${build}-clang" --print-errorlogs
  clang=passed
fi

for specification in \
  'server-historical -Dexperimental=false -Dlibgwipc=false -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'server-output -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'gwm -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false' \
  'compositor-software-headless -Dexperimental=true -Drender_gl=false -Ddrm_backend=false -Dheadless_backend=true -Dglasswyrmd=false -Dgwm=false' \
  'compositor-gles-headless -Dexperimental=true -Drender_gl=true -Ddrm_backend=false -Dheadless_backend=true -Dglasswyrmd=false -Dgwm=false' \
  'compositor-software-drm -Dexperimental=true -Drender_gl=false -Ddrm_backend=true -Dheadless_backend=false -Dglasswyrmd=false -Dgwm=false' \
  'compositor-gles-drm -Dexperimental=true -Drender_gl=true -Ddrm_backend=true -Dheadless_backend=false -Dglasswyrmd=false -Dgwm=false' \
  'ipc -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'session -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false'; do
  read -r -a fields <<<"$specification"
  name=${fields[0]}; unset 'fields[0]'
  setup_build "${build}-components/$name" "${fields[@]}"
done
setup_build "$tools_build" -Dexperimental=true -Dlibgwipc=true \
  -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=true
result[component_builds]=passed
"$source_dir/tests/tools/source_layout_test.sh" | tee "$artifact_dir/milestone13-source-layout.log"
result[source_layout]=passed
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" "$software"
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" \
  "${build}-components/ipc"
result[api_consumers]=passed
meson test -C "$software" --print-errorlogs m13-fixture-bootstrap \
  m13-fixture-validator m13-evidence-validator output-configuration-process \
  output-configuration-coordinator
result[m1_m12_regressions]=passed
result[stale_rejection]=passed result[busy_rejection]=passed

find_target() {
  local directory=$1 name=$2 path
  path=$(find "$directory" -type f -name "$name" -perm -0100 -print -quit)
  [[ -n $path ]] || { printf 'Missing Meson target %s in %s\n' "$name" "$directory" >&2; return 1; }
  printf '%s\n' "$path"
}
scale_client=$(find_target "$software" m13_scale_client)
uinput_helper=$(find_target "$software" gw_uinput_m11)
synthetic_input=$(find_target "$software" gwinput_m8)

wait_socket() {
  local path=$1
  for _ in {1..400}; do [[ -S $path ]] && return; sleep .05; done
  printf 'Timed out waiting for M13 socket: %s\n' "$path" >&2
  return 1
}
start_headless_peers() {
  local tree=$1 suffix=$2 renderer_name=$3 renderer_report=$4 scene=$5
  systemd-run --unit="gwm-m13$suffix" --property=Type=simple --no-block -- \
    "$tree/src/gwm" --ipc-socket "$runtime/gwm.sock"
  wait_socket "$runtime/gwm.sock"
  systemd-run --unit="gwcomp-m13$suffix" --property=Type=simple --no-block -- \
    "$tree/src/gwcomp" --backend headless --ipc-socket "$runtime/gwcomp.sock" \
    --dump-dir "$runtime/frames" --headless-output LEFT:640x480@60000 \
    --headless-output RIGHT:800x600@60000 --renderer "$renderer_name" \
    --renderer-report "$renderer_report" --scene-manifest "$scene"
  wait_socket "$runtime/gwcomp.sock"
  systemd-run --unit="glasswyrmd-m13$suffix" --property=Type=simple --no-block -- \
    "$tree/src/glasswyrmd" --display 99 --wm-socket "$runtime/gwm.sock" \
    --compositor-socket "$runtime/gwcomp.sock" --software-content \
    --output-model --control-socket "$runtime/control.sock" --scale-protocol \
    --synthetic-input-socket "$runtime/input.sock" \
    --game-compat
  wait_socket "$runtime/control.sock"
  wait_socket "$runtime/input.sock"
  wait_socket /tmp/.X11-unix/X99
}
stop_headless_peers() {
  local suffix=$1
  local unit
  for unit in "glasswyrmd-m13$suffix.service" "gwcomp-m13$suffix.service" \
    "gwm-m13$suffix.service"; do
    systemctl stop "$unit"
    [[ $(systemctl show "$unit" -p LoadState --value) == loaded ]]
    [[ $(systemctl show "$unit" -p ExecMainStatus --value) == 0 ]]
    service_checks=$((service_checks + 1))
  done
}

failure_stage='headless-runtime'
export PATH="$software/src:$software/tools:$software/tests:$PATH"
start_headless_peers "$software" '' software \
  "$artifact_dir/milestone13-renderer-software.jsonl" "$scenes/scene.jsonl"
[[ $(stat -c %a "$runtime/control.sock") == 600 ]]
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs \
  >"$artifact_dir/milestone13-randr.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$artifact_dir/milestone13-gwinfo-outputs.json"
"$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$artifact_dir/milestone13-gwinfo-windows.json"
cp "$artifact_dir/milestone13-gwinfo-outputs.json" \
  "$artifact_dir/milestone13-output-inventory.json"
"$software/tools/gwout" --socket "$runtime/control.sock" list --json \
  >"$artifact_dir/milestone13-layout-before.json"
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
  --position 640,0 --scale 5/4 --transform normal --json \
  >"$artifact_dir/milestone13-gwout-result.json" \
  2>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwout" --socket "$runtime/control.sock" list --json \
  >"$artifact_dir/milestone13-layout-after.json"
read -r layout_generation output_ids left_id right_id < <(python3 - \
  "$artifact_dir/milestone13-layout-after.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
ids={item['name']:item['id'] for item in d['outputs']}
print(d['layout_generation'],','.join(item['id'] for item in d['outputs']),
      ids['LEFT'],ids['RIGHT'])
PY
)
grep -Fq LEFT "$artifact_dir/milestone13-randr.log"
grep -Fq RIGHT "$artifact_dir/milestone13-randr.log"
python3 - "$artifact_dir/milestone13-gwinfo-outputs.json" \
  "$artifact_dir/milestone13-gwinfo-windows.json" \
  "$artifact_dir/milestone13-gwout-result.json" <<'PY'
import json,sys
outputs,windows,commit=(json.load(open(path)) for path in sys.argv[1:])
GWIPC_OUTPUT_CONFIGURATION_ACCEPTED=1
assert [item['name'] for item in outputs['outputs']]==['LEFT','RIGHT']
assert len({item['id'] for item in outputs['outputs']})==2
assert windows['windows']==[]
assert (commit['result']==GWIPC_OUTPUT_CONFIGURATION_ACCEPTED
        and commit['applied_generation']>0)
assert (commit['root_width'],commit['root_height'])==(1280,480)
assert commit['enabled_output_count']==2
PY
result[gwinfo_text]=passed result[gwinfo_json]=passed result[gwout_commit]=passed
result[output_inventory]=passed
python3 - "$artifact_dir/milestone13-layout-before.json" \
  "$artifact_dir/milestone13-layout-after.json" <<'PY'
import json,sys
before,after=(json.load(open(path)) for path in sys.argv[1:])
assert (before['root_width'],before['root_height'])==(1440,600)
assert (after['root_width'],after['root_height'])==(1280,480)
assert after['layout_generation']==before['layout_generation']+1
right=next(item for item in after['outputs'] if item['name']=='RIGHT')
assert (right['logical_x'],right['logical_y'])==(640,0)
assert (right['scale_numerator'],right['scale_denominator'])==(5,4)
PY
result[logical_physical_geometry]=passed result[fractional_scaling]=passed

DISPLAY=:99 XAUTHORITY=/dev/null SDL_VIDEODRIVER=x11 \
  SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
  LD_LIBRARY_PATH="$clients/install/lib64:$clients/install/lib" \
  "$clients/install/bin/m13_sdl_display_probe" \
  --output "$artifact_dir/milestone13-sdl-displays.json"
python3 - "$artifact_dir/milestone13-sdl-displays.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d['sdl_version']=='2.32.10' and d['driver']=='x11'
assert d['display_count']==2 and d['passed'] is True
assert {(item['current_mode']['width'],item['current_mode']['height'])
        for item in d['displays']}=={(640,480),(800,600)}
assert all(item['mode_count']>0 for item in d['displays'])
PY
result[sdl_display_discovery]=passed

for order in little big; do
  ready="$control/randr-$order-ready" trigger="$control/randr-$order-trigger"
  "$source_dir/tests/compat/m13/m13_raw_output_probe.py" --display 99 \
    --byte-order "$order" --scenario randr --event-ready "$ready" \
    --event-trigger "$trigger" \
    --output "$artifact_dir/milestone13-randr-$order.json" &
  randr_pid=$!
  for _ in {1..400}; do [[ -s $ready ]] && break; sleep .05; done
  [[ -s $ready ]]
  "$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
    --scale 6/5 --json >>"$artifact_dir/milestone13-gwout.log"
  : >"$trigger"
  wait "$randr_pid"
  randr_pid=0
  "$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
    --scale 5/4 --json >>"$artifact_dir/milestone13-gwout.log"
  "$source_dir/tests/compat/m13/m13_raw_output_probe.py" --display 99 \
    --byte-order "$order" --scenario gw-scale \
    --output "$artifact_dir/milestone13-gw-scale-$order.json"
done
cp "$artifact_dir/milestone13-gw-scale-little.json" \
  "$artifact_dir/milestone13-gw-scale.log"
result[multi_output_randr]=passed

frame_count() { find "$runtime/frames" -type f -name '*.ppm' | wc -l; }
frame_set_count() {
  [[ -f $runtime/frames/frame-sets.jsonl ]] && \
    wc -l <"$runtime/frames/frame-sets.jsonl" || printf '0\n'
}
wait_frame_set_after() {
  local before=$1
  for _ in {1..400}; do (( $(frame_set_count) > before )) && return; sleep .05; done
  printf 'Timed out waiting for the next M13 frame-set transaction.\n' >&2
  return 1
}
wait_frames_after() {
  local before=$1
  for _ in {1..400}; do (( $(frame_count) >= before + 2 )) && return; sleep .05; done
  printf 'Timed out waiting for the next atomic M13 frame set.\n' >&2
  return 1
}
copy_latest_output() {
  local id=$1 target=$2 path
  path=$(python3 - "$runtime/frames/frame-sets.jsonl" "$runtime/frames" "$id" <<'PY'
import json,pathlib,sys
manifest,root,output_id=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]),sys.argv[3]
for line in reversed(manifest.read_text().splitlines()):
  record=json.loads(line)
  for output in record['outputs']:
    if output['output_id']==output_id:
      matches=list(root.rglob(output['file']))
      if len(matches)!=1: raise SystemExit('frame-set file did not resolve uniquely')
      print(matches[0]); raise SystemExit(0)
raise SystemExit('output ID is absent from the frame-set manifest')
PY
)
  [[ -n $path ]] || { printf 'No frame found for output %s.\n' "$id" >&2; return 1; }
  cp "$path" "$target"
}
legacy_command() {
  python3 - "$control/legacy.sock" "$@" <<'PY'
import socket,sys
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(sys.argv[1])
s.sendall((' '.join(sys.argv[2:])+'\n').encode('ascii'))
assert s.recv(32)==b'ok\n'
PY
}
assert_window_memberships() {
  local expected=$1 output=$2
  "$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json >"$output"
  python3 - "$output" "$expected" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); expected=set(sys.argv[2].split(','))
assert len(d['windows'])==1
w=d['windows'][0]
assert set(w['output_ids'])==expected
assert w['scale_mode']=='legacy' and w['client_buffer_scale']==1
PY
}

before_frames=$(frame_count)
"$source_dir/tests/compat/m13/m13_legacy_client.py" --display 99 \
  --control-socket "$control/legacy.sock" --ready "$control/legacy-ready.json" &
legacy_pid=$!
for _ in {1..400}; do [[ -S $control/legacy.sock && -s $control/legacy-ready.json ]] && break; sleep .05; done
wait_frames_after "$before_frames"
assert_window_memberships "$left_id" "$control_data/legacy-left.json"
copy_latest_output "$left_id" "$control_data/milestone13-legacy-left.ppm"

scene_line_before=$(wc -l <"$scenes/scene.jsonl")
crossing_release=$control_data/pointer-release
crossing_steps=$control_data/pointer-steps
rm -f "$crossing_release"
install -d -m 0700 "$crossing_steps"
"$synthetic_input" --socket "$runtime/input.sock" --scenario crossing \
  --output "$control_data/pointer-acks.json" \
  --step-directory "$crossing_steps" \
  --hold-until "$crossing_release" &
synthetic_pid=$!
wait_crossing_step() {
  local input_id=$1
  for _ in {1..400}; do
    [[ -s $crossing_steps/step-$input_id.ready ]] && return
    sleep .05
  done
  return 1
}
wait_cursor_position() {
  local expected_id=$1 expected_x=$2 expected_y=$3
  for _ in {1..400}; do
    if python3 - "$scenes/scene.jsonl" "$scene_line_before" \
      "$expected_id" "$expected_x" "$expected_y" 2>/dev/null <<'PY'
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]).read().splitlines()[int(sys.argv[2]):]]
expected,x,y=int(sys.argv[3],16),int(sys.argv[4]),int(sys.argv[5])
assert any((cursor.get('x'),cursor.get('y'))==(x,y) and
           expected in cursor.get('memberships',[])
           for record in records for cursor in record.get('cursors',[]))
PY
    then
      return
    fi
    sleep .05
  done
  return 1
}
wait_crossing_step 2
wait_cursor_position "$left_id" 100 100
: >"$crossing_steps/step-2.release"
wait_crossing_step 3
wait_cursor_position "$right_id" 700 479
: >"$crossing_steps/step-3.release"
for _ in {1..400}; do
  [[ -s $control_data/pointer-acks.json ]] && break
  sleep .05
done
[[ -s $control_data/pointer-acks.json ]]
python3 - "$control_data/pointer-acks.json" "$scenes/scene.jsonl" \
  "$scene_line_before" "$left_id" "$right_id" \
  "$artifact_dir/milestone13-pointer-crossing.json" <<'PY'
import json,sys
acks=json.load(open(sys.argv[1]))
records=[json.loads(line) for line in open(sys.argv[2]).read().splitlines()[int(sys.argv[3]):]]
events=acks['acknowledgements']
assert acks['scenario']=='crossing' and len(events)==4
assert (events[1]['root_x'],events[1]['root_y'])==(100,100)
assert events[2]['root_x']==700 and events[2]['root_y']==479
assert events[1]['result']=='accepted' and events[2]['result'] in ('accepted','clamped')
assert all(record.get('schema')=='glasswyrm-scene-v2' for record in records)
cursor_ids=[output_id for record in records
            for cursor in record.get('cursors',[])
            for output_id in cursor.get('memberships',[])]
left,right=int(sys.argv[4],16),int(sys.argv[5],16)
assert left in cursor_ids and right in cursor_ids
assert cursor_ids.index(left)<len(cursor_ids)-1-cursor_ids[::-1].index(right)
json.dump({'schema':1,'passed':True,'acknowledgements':events,
           'cursor_output_ids':cursor_ids},open(sys.argv[6],'w'),sort_keys=True)
PY
: >"$crossing_release"
wait "$synthetic_pid"
synthetic_pid=0
result[pointer_output_crossing]=passed

before_frames=$(frame_count); legacy_command fullscreen on
wait_frames_after "$before_frames"
"$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$control_data/fullscreen-left.json"
python3 - "$control_data/fullscreen-left.json" "$left_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert len(d['windows'])==1
w=d['windows'][0]
assert w['fullscreen'] is True and w['primary_output_id']==sys.argv[2]
assert (w['logical_x'],w['logical_y'],w['logical_width'],w['logical_height'])==(0,0,640,480)
PY
before_frames=$(frame_count); legacy_command fullscreen off
wait_frames_after "$before_frames"
"$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$control_data/fullscreen-left-restored.json"

before_frames=$(frame_count); legacy_command configure 480 80 320 240
wait_frames_after "$before_frames"
assert_window_memberships "$left_id,$right_id" "$control_data/legacy-spanning.json"
copy_latest_output "$left_id" "$control_data/milestone13-legacy-spanning-left.ppm"
copy_latest_output "$right_id" "$control_data/milestone13-legacy-spanning-right.ppm"

before_frames=$(frame_count); legacy_command configure 760 80 320 240
wait_frames_after "$before_frames"
assert_window_memberships "$right_id" "$control_data/legacy-right.json"
copy_latest_output "$right_id" "$control_data/milestone13-legacy-right.ppm"

before_frames=$(frame_count); legacy_command fullscreen on
wait_frames_after "$before_frames"
"$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$control_data/fullscreen-right.json"
python3 - "$control_data/fullscreen-right.json" "$right_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert len(d['windows'])==1
w=d['windows'][0]
assert w['fullscreen'] is True and w['primary_output_id']==sys.argv[2]
assert (w['logical_x'],w['logical_y'],w['logical_width'],w['logical_height'])==(640,0,640,480)
PY
before_frames=$(frame_count); legacy_command fullscreen off
wait_frames_after "$before_frames"
"$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$control_data/fullscreen-right-restored.json"
python3 - "$control_data/fullscreen-left.json" \
  "$control_data/fullscreen-left-restored.json" \
  "$control_data/fullscreen-right.json" \
  "$control_data/fullscreen-right-restored.json" \
  "$artifact_dir/milestone13-fullscreen-outputs.json" <<'PY'
import json,sys
left,left_restored,right,right_restored=(json.load(open(path))['windows'][0]
                                         for path in sys.argv[1:5])
assert left['fullscreen'] is True and right['fullscreen'] is True
assert left_restored['fullscreen'] is False and right_restored['fullscreen'] is False
assert (left_restored['logical_width'],left_restored['logical_height'])==(320,240)
assert (right_restored['logical_width'],right_restored['logical_height'])==(320,240)
json.dump({'schema':1,'passed':True,'left':left,'right':right,
           'left_restored':left_restored,'right_restored':right_restored},
          open(sys.argv[5],'w'),sort_keys=True)
PY
result[fullscreen_outputs]=passed

before_sets=$(frame_set_count)
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT --disable \
  --json >>"$artifact_dir/milestone13-gwout.log"
wait_frame_set_after "$before_sets"
"$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
  >"$control_data/right-disabled.json"
python3 - "$control_data/right-disabled.json" "$left_id" "$right_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); left,right=sys.argv[2:]
states={item['id']:item for item in d['outputs']}
assert d['primary_output_id']==left and (d['root_width'],d['root_height'])==(640,480)
assert states[left]['enabled'] is True and states[right]['enabled'] is False
assert len(d['windows'])==1 and d['windows'][0]['output_ids']==[left]
PY
before_sets=$(frame_set_count)
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT --enable \
  --position 640,0 --scale 5/4 --transform normal --json \
  >>"$artifact_dir/milestone13-gwout.log"
wait_frame_set_after "$before_sets"
"$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
  >"$control_data/right-reenabled.json"
python3 - "$control_data/right-reenabled.json" "$left_id" "$right_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); left,right=sys.argv[2:]
states={item['id']:item for item in d['outputs']}
assert d['primary_output_id']==left and (d['root_width'],d['root_height'])==(1280,480)
assert states[right]['enabled'] is True
assert (states[right]['logical_x'],states[right]['logical_y'])==(640,0)
assert (states[right]['scale_numerator'],states[right]['scale_denominator'])==(5,4)
assert len(d['windows'])==1 and d['windows'][0]['output_ids']==[right]
PY
result[output_enable_disable]=passed

before_frames=$(frame_count); legacy_command configure 0 0 1280 480
wait_frames_after "$before_frames"
assert_window_memberships "$left_id,$right_id" \
  "$artifact_dir/milestone13-gwinfo-windows.json"
result[surface_membership]=passed result[legacy_fallback]=passed

"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT --primary \
  --json >>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/right-primary.json"
python3 - "$control_data/right-primary.json" "$right_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert d['primary_output_id']==sys.argv[2]
PY
"$software/tools/gwout" --socket "$runtime/control.sock" set LEFT --primary \
  --json >>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/left-primary-restored.json"
python3 - "$control_data/left-primary-restored.json" "$left_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert d['primary_output_id']==sys.argv[2]
PY
result[primary_transition]=passed

"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT --scale 2/1 \
  --json >>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/integer-scale.json"
python3 - "$control_data/integer-scale.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); r=next(x for x in d['outputs'] if x['name']=='RIGHT')
assert (r['scale_numerator'],r['scale_denominator'])==(2,1)
assert (r['logical_width'],r['logical_height'])==(400,300)
PY
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT --scale 5/4 \
  --json >>"$artifact_dir/milestone13-gwout.log"
result[integer_scaling]=passed

scale_initial_ready=$control/scale-initial-ready.json
scale_moved_ready=$control/scale-moved-ready.json
before_frames=$(frame_count)
DISPLAY=:99 "$scale_client" --display 99 --byte-order little --move-x 700 \
  --initial-hold-ms 10000 --initial-ready-file "$scale_initial_ready" \
  --hold-ms 10000 --ready-file "$scale_moved_ready" \
  --result "$artifact_dir/milestone13-scale-client.json" &
scale_pid=$!
for _ in {1..400}; do [[ -s $scale_initial_ready ]] && break; sleep .05; done
[[ -s $scale_initial_ready ]]
python3 - "$scale_initial_ready" <<'PY'
import json,os,stat,sys
d=json.load(open(sys.argv[1])); assert d['state']=='scaled-pixmap-initial'
assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode)==0o600
PY
wait_frames_after "$before_frames"
copy_latest_output "$left_id" "$control_data/milestone13-aware-left.ppm"
before_frames=$(frame_count)
for _ in {1..400}; do [[ -s $scale_moved_ready ]] && break; sleep .05; done
[[ -s $scale_moved_ready ]]
python3 - "$scale_moved_ready" <<'PY'
import json,os,stat,sys
d=json.load(open(sys.argv[1])); assert d['state']=='scaled-pixmap-moved'
assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode)==0o600
PY
wait_frames_after "$before_frames"
copy_latest_output "$right_id" "$control_data/milestone13-aware-right.ppm"
wait "$scale_pid"
scale_pid=0
python3 - "$artifact_dir/milestone13-scale-client.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d['schema']=='glasswyrm.m13-scale-client.v1'
assert d['initial']['memberships']!=d['moved']['memberships']
assert d['present_serial']==1 and d['reset_scale']==1
PY
result[scaled_pixmap]=passed result[gw_scale_events]=passed

before_frames=$(frame_count)
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
  --transform rotate-90 --json >>"$artifact_dir/milestone13-gwout.log"
wait_frames_after "$before_frames"
copy_latest_output "$right_id" "$control_data/milestone13-rotate90.ppm"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/rotate90-layout.json"
before_frames=$(frame_count)
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
  --transform flipped --json >>"$artifact_dir/milestone13-gwout.log"
wait_frames_after "$before_frames"
copy_latest_output "$right_id" "$control_data/milestone13-flipped.ppm"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/flipped-layout.json"
python3 - "$control_data/rotate90-layout.json" \
  "$control_data/flipped-layout.json" <<'PY'
import json,sys
rotate,flip=(json.load(open(path)) for path in sys.argv[1:])
r=next(x for x in rotate['outputs'] if x['name']=='RIGHT')
f=next(x for x in flip['outputs'] if x['name']=='RIGHT')
assert r['transform']=='rotate-90' and (r['logical_width'],r['logical_height'])==(480,640)
assert f['transform']=='flipped' and (f['logical_width'],f['logical_height'])==(640,480)
PY
result[transforms]=passed

"$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
  >"$control_data/restart-before.json"
copy_latest_output "$left_id" "$control_data/restart-left-before.ppm"
copy_latest_output "$right_id" "$control_data/restart-right-before.ppm"
systemctl restart gwm-m13.service
wait_socket "$runtime/gwm.sock"
[[ $(systemctl show gwm-m13.service -p ActiveState --value) == active ]]
for _ in {1..400}; do
  "$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
    >"$control_data/after-gwm.json" 2>/dev/null && break
  sleep .05
done
[[ -s $control_data/after-gwm.json ]]
systemctl stop gwcomp-m13.service
[[ $(systemctl show gwcomp-m13.service -p ExecMainStatus --value) == 0 ]]
[[ $(systemctl show gwcomp-m13.service -p MainPID --value) == 0 ]]
[[ ! -S $runtime/gwcomp.sock ]]
install -d -m 0755 "$control_data/pre-restart-frames"
find "$runtime/frames" -mindepth 1 -maxdepth 1 \
  -exec mv -t "$control_data/pre-restart-frames" -- {} +
mv "$artifact_dir/milestone13-renderer-software.jsonl" \
  "$control_data/milestone13-renderer-software-pre-restart.jsonl"
systemctl start gwcomp-m13.service
wait_socket "$runtime/gwcomp.sock"
wait_frames_after 0
[[ -s $runtime/frames/frame-sets.jsonl ]]
[[ $(systemctl show gwcomp-m13.service -p ActiveState --value) == active ]]
for _ in {1..400}; do
  "$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
    >"$artifact_dir/milestone13-restart.json" 2>/dev/null && break
  sleep .05
done
[[ -s $artifact_dir/milestone13-restart.json ]]
copy_latest_output "$left_id" "$control_data/restart-left-after.ppm"
copy_latest_output "$right_id" "$control_data/restart-right-after.ppm"
python3 - "$control_data/restart-before.json" \
  "$control_data/after-gwm.json" \
  "$artifact_dir/milestone13-restart.json" \
  "$control_data/restart-left-before.ppm" "$control_data/restart-left-after.ppm" \
  "$control_data/restart-right-before.ppm" "$control_data/restart-right-after.ppm" <<'PY'
import hashlib,json,sys
before,after_gwm,after=(json.load(open(path)) for path in sys.argv[1:4])
for key in ('layout_generation','root_width','root_height','primary_output_id','outputs','windows'):
  assert before[key]==after_gwm[key],('gwm',key)
  assert before[key]==after[key],('compositor',key)
for left,right in ((sys.argv[4],sys.argv[5]),(sys.argv[6],sys.argv[7])):
  assert hashlib.sha256(open(left,'rb').read()).digest()==hashlib.sha256(open(right,'rb').read()).digest()
PY
legacy_command stop
wait "$legacy_pid"
legacy_pid=0
"$software/tools/gwinfo" --socket "$runtime/control.sock" all --json \
  >"$control_data/post-legacy-cleanup.json"
result[gwm_replay]=passed result[compositor_replay]=passed result[stable_id_replay]=passed

failure_stage='headless-evidence'
install -d -m 0755 "$headless/evidence" "$headless/compare-software" \
  "$headless/compare-gles"
cp -a "$runtime/frames/." "$headless/evidence/"
cp "$runtime/frames/frame-sets.jsonl" \
  "$artifact_dir/milestone13-frame-sets.jsonl"
headless_aggregate_hash=$("$source_dir/tests/compat/m13/validate_frame_sets.py" \
  "$artifact_dir/milestone13-frame-sets.jsonl" "$runtime/frames" \
  --print-last-aggregate)
(cd "$headless/evidence" && tar -cf \
  "$artifact_dir/milestone13-headless-outputs.tar" .)
result[headless_frame_hashes]=passed result[aggregate_hash]=passed

stop_headless_peers ''
cat "$control_data/milestone13-renderer-software-pre-restart.jsonl" \
  "$artifact_dir/milestone13-renderer-software.jsonl" \
  >"$control_data/milestone13-renderer-software.jsonl"
mv "$control_data/milestone13-renderer-software.jsonl" \
  "$artifact_dir/milestone13-renderer-software.jsonl"
rm -rf -- "$runtime"; install -d -m 0700 "$runtime"
start_headless_peers "$software" -compare software \
  "$control_data/compare-software-renderer.jsonl" \
  "$scenes/compare-software-scene.jsonl"
"$software/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
  --position 640,0 --scale 5/4 --transform normal --json \
  >"$control_data/compare-software-layout.json"
before_frames=$(frame_count)
"$source_dir/tests/compat/m13/m13_legacy_client.py" --display 99 \
  --control-socket "$control/legacy.sock" \
  --ready "$control/compare-software-ready.json" &
compare_software_pid=$!
for _ in {1..400}; do [[ -S $control/legacy.sock && -s $control/compare-software-ready.json ]] && break; sleep .05; done
wait_frames_after "$before_frames"
before_frames=$(frame_count); legacy_command configure 700 80 320 240
wait_frames_after "$before_frames"
assert_window_memberships "$right_id" "$control_data/compare-software-right.json"
find "$runtime/frames" -type f -name '*.ppm' -exec cp -t "$headless/compare-software" -- {} +
legacy_command stop; wait "$compare_software_pid"
stop_headless_peers -compare
rm -rf -- "$runtime"; install -d -m 0700 "$runtime"
start_headless_peers "$gles" -gles gles \
  "$artifact_dir/milestone13-renderer-gles.jsonl" "$scenes/gles-scene.jsonl"
"$gles/tools/gwout" --socket "$runtime/control.sock" set RIGHT \
  --position 640,0 --scale 5/4 --transform normal --json \
  >"$control_data/gles-layout.json"
before_frames=$(frame_count)
"$source_dir/tests/compat/m13/m13_legacy_client.py" --display 99 \
  --control-socket "$control/legacy.sock" --ready "$control/gles-legacy-ready.json" &
gles_legacy_pid=$!
for _ in {1..400}; do [[ -S $control/legacy.sock && -s $control/gles-legacy-ready.json ]] && break; sleep .05; done
wait_frames_after "$before_frames"
before_frames=$(frame_count); legacy_command configure 700 80 320 240
wait_frames_after "$before_frames"
"$gles/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
  >"$control_data/compare-gles-right.json"
python3 - "$control_data/compare-gles-right.json" "$right_id" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert len(d['windows'])==1
assert d['windows'][0]['output_ids']==[sys.argv[2]]
PY
find "$runtime/frames" -type f -name '*.ppm' -exec cp -t "$headless/compare-gles" -- {} +
legacy_command stop
wait "$gles_legacy_pid"
gles_legacy_pid=0
"$source_dir/tests/compat/m13/compare_output_frames.py" \
  --software-dir "$headless/compare-software" \
  --gles-dir "$headless/compare-gles" \
  --output "$artifact_dir/milestone13-renderer-fractional-diff.json"
result[renderer_comparison]=passed

failure_stage='drm-runtime'
stop_headless_peers -gles
rm -rf -- "$runtime"; install -d -m 0700 "$runtime"
systemd-run --unit=gw-uinput-m13.service --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property='DeviceAllow=/dev/uinput rw' \
  --no-block -- "$uinput_helper" serve \
  --control-socket "$control/input.sock" --devices-json "$control/devices.json"
for _ in {1..400}; do [[ -s $control/devices.json && -S $control/input.sock ]] && break; sleep .05; done
readarray -t input_paths < <(python3 - "$control/devices.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); print(d['keyboard']['event_path']); print(d['pointer']['event_path'])
PY
)
keyboard=${input_paths[0]} pointer=${input_paths[1]}
capture_vt_state() {
  python3 - "$target_vt" "$1" <<'PY'
import fcntl,json,os,struct,sys
tty,output=sys.argv[1:]
fd=os.open(tty,os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
  active=bytearray(struct.calcsize('=HHH')); mode=bytearray(struct.calcsize('=BBhhh'))
  kd=bytearray(struct.calcsize('=i')); keyboard=bytearray(struct.calcsize('=i'))
  fcntl.ioctl(fd,0x5603,active,True); fcntl.ioctl(fd,0x5601,mode,True)
  fcntl.ioctl(fd,0x4B3B,kd,True); fcntl.ioctl(fd,0x4B44,keyboard,True)
finally: os.close(fd)
json.dump({'active':struct.unpack('=HHH',active),'mode':struct.unpack('=BBhhh',mode),
 'kd':struct.unpack('=i',kd)[0],'keyboard':struct.unpack('=i',keyboard)[0]},
 open(output,'w'),sort_keys=True)
PY
}
getty_unit=getty@${target_vt##*/}.service
getty_active_before=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_before=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
logind_active_before=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
logind_socket_active_before=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
logind_enabled_before=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
logind_socket_enabled_before=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
original_vt=$(python3 - /dev/tty0 <<'PY'
import fcntl,os,struct,sys
fd=os.open(sys.argv[1],os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
  state=bytearray(struct.calcsize('=HHH')); fcntl.ioctl(fd,0x5603,state,True)
finally: os.close(fd)
print(struct.unpack('=HHH',state)[0])
PY
)
getty_state_captured=true logind_state_captured=true
"$software/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --snapshot-state \
  --output "$artifact_dir/milestone13-kms-before.json"
capture_vt_state "$artifact_dir/milestone13-vt-before.json"
[[ $getty_active_before != active ]] || systemctl stop "$getty_unit"
systemctl mask --runtime --now "$logind_socket" "$logind_unit"
systemd-run --unit=glasswyrm-m13-drm --property=Type=simple \
  --property=PrivateDevices=no --property=DevicePolicy=closed \
  --property="DeviceAllow=$drm_device rw" --property="DeviceAllow=$target_vt rw" \
  --property=StandardInput=tty-force --property="TTYPath=$target_vt" \
  --property=TTYReset=yes --property=TTYVHangup=yes --property=TTYVTDisallocate=no \
  --no-block -- "$software/src/glasswyrm-session" --runtime-dir "$runtime" \
  --display 99 --backend drm --drm-device "$drm_device" --tty "$target_vt" \
  --connector "$connector" --mode 1024x768 --drm-api auto \
  --input-device "$keyboard" --input-device "$pointer" --output-model \
  --control-socket "$runtime/control.sock" --scale-protocol --game-compat \
  --renderer software --mirror-dump-dir "$drm" \
  --renderer-report "$artifact_dir/milestone13-renderer-drm.jsonl" \
  --drm-report "$artifact_dir/milestone13-drm-report.jsonl"
for _ in {1..400}; do [[ -S $runtime/control.sock ]] && break; sleep .05; done
"$software/tools/gwout" --socket "$runtime/control.sock" set "$connector" \
  --scale 4/3 --transform rotate-180 --json >>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/drm-scaled.json"
python3 - "$control_data/drm-scaled.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert (d['root_width'],d['root_height'])==(768,576)
assert len(d['outputs'])==1
o=d['outputs'][0]
assert (o['scale_numerator'],o['scale_denominator'])==(4,3)
assert o['transform']=='rotate-180'
PY
result[drm_scale_transform]=passed
drm_frames_before=$(find "$drm" -type f -name '*.ppm' | wc -l)
"$source_dir/tests/compat/m13/m13_legacy_client.py" --display 99 \
  --control-socket "$control/legacy.sock" --ready "$control/drm-legacy-ready.json" &
drm_legacy_pid=$!
for _ in {1..400}; do
  [[ -S $control/legacy.sock && -s $control/drm-legacy-ready.json ]] && break
  sleep .05
done
[[ -S $control/legacy.sock && -s $control/drm-legacy-ready.json ]]
for _ in {1..400}; do
  "$software/tools/gwinfo" --socket "$runtime/control.sock" windows --json \
    >"$control_data/drm-legacy-window.json" 2>/dev/null && \
    python3 -c 'import json,sys; assert len(json.load(open(sys.argv[1]))["windows"])==1' \
      "$control_data/drm-legacy-window.json" && break
  sleep .05
done
python3 - "$control_data/drm-scaled.json" \
  "$control_data/drm-legacy-window.json" <<'PY'
import json,sys
outputs,windows=(json.load(open(path)) for path in sys.argv[1:])
assert len(outputs['outputs'])==1 and len(windows['windows'])==1
window=windows['windows'][0]
assert window['output_ids']==[outputs['outputs'][0]['id']]
assert window['scale_mode']=='legacy' and window['client_buffer_scale']==1
PY
for _ in {1..400}; do
  (( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before )) && break
  sleep .05
done
(( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before ))
drm_frames_before=$(find "$drm" -type f -name '*.ppm' | wc -l)
"$uinput_helper" run --control-socket "$control/input.sock" \
  --scenario pointer-anchor --result-json "$control_data/drm-pointer-input.json"
python3 - "$control_data/drm-pointer-input.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert d['scenario']=='pointer-anchor'
assert d['status']=='completed' and d['event_count']>0
PY
for _ in {1..400}; do
  (( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before )) && break
  sleep .05
done
(( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before ))
printf 'ready\nmode=1024x768\n' >"$control/drm-screen-ready"
for _ in {1..18000}; do [[ -f $control/drm-screen-captured ]] && break; sleep .1; done
grep -Fxq screen-captured "$control/drm-screen-captured"
find "$drm" -type f -name '*.ppm' -print | sort -V | tail -n1 | \
  xargs -r cp -t "$artifact_dir"
canonical=$(find "$artifact_dir" -maxdepth 1 -type f -name '*.ppm' \
  ! -name 'milestone13-drm-screen.ppm' -print | sort | tail -n1)
cp "$canonical" "$artifact_dir/milestone13-drm-canonical.ppm"
cmp "$artifact_dir/milestone13-drm-canonical.ppm" \
  "$artifact_dir/milestone13-drm-screen.ppm"
result[screenshot_equality]=passed
drm_frames_before=$(find "$drm" -type f -name '*.ppm' | wc -l)
chvt 1; chvt 2
"$uinput_helper" run --control-socket "$control/input.sock" --scenario post-vt \
  --result-json "$control_data/post-vt-input.json"
python3 - "$control_data/post-vt-input.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert d['scenario']=='post-vt'
assert d['status']=='completed' and d['event_count']>0
PY
for _ in {1..400}; do
  (( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before )) && break
  sleep .05
done
(( $(find "$drm" -type f -name '*.ppm' | wc -l) > drm_frames_before ))
post_vt_frame=$(find "$drm" -type f -name '*.ppm' -print | sort -V | tail -n1)
cmp "$artifact_dir/milestone13-drm-canonical.ppm" "$post_vt_frame"
result[vt_replay]=passed
legacy_command stop
wait "$drm_legacy_pid"
drm_legacy_pid=0
"$software/tools/gwout" --socket "$runtime/control.sock" set "$connector" \
  --scale 1/1 --transform normal --json >>"$artifact_dir/milestone13-gwout.log"
"$software/tools/gwinfo" --socket "$runtime/control.sock" outputs --json \
  >"$control_data/drm-restored-layout.json"
python3 - "$control_data/drm-restored-layout.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); o=d['outputs'][0]
assert (o['scale_numerator'],o['scale_denominator'])==(1,1)
assert o['transform']=='normal'
PY
systemctl stop glasswyrm-m13-drm.service
[[ $(systemctl show glasswyrm-m13-drm.service -p LoadState --value) == loaded ]]
[[ $(systemctl show glasswyrm-m13-drm.service -p ExecMainStatus --value) == 0 ]]
[[ $(systemctl show glasswyrm-m13-drm.service -p MainPID --value) == 0 ]]
python3 - "$artifact_dir/milestone13-drm-report.jsonl" \
  "$artifact_dir/milestone13-renderer-drm.jsonl" \
  "$artifact_dir/milestone13-drm-canonical.ppm" "$post_vt_frame" \
  "$artifact_dir/milestone13-drm-representation.json" <<'PY'
import hashlib,json,sys
drm=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
renderer=[json.loads(line) for line in open(sys.argv[2]) if line.strip()]
assert drm and renderer and not any(item['record']=='fatal' for item in drm)
releases=[item for item in drm if item['record']=='vt' and item['transition']=='release']
acquires=[item for item in drm if item['record']=='vt' and item['transition']=='acquire']
assert releases and acquires
assert acquires[-1]['master_owned'] is True and acquires[-1]['full_modeset'] is True
assert releases[-1]['committed_hash']==acquires[-1]['committed_hash']!='0000000000000000'
restore=[item for item in drm if item['record']=='restore'][-1]
assert all(restore[key] is True for key in ('kms','vt','master_drop','framebuffer_cleanup'))
frames=[item for item in renderer if item['record']=='output-frame']
assert frames and all(item['selected']=='software' and item['error'] is None
                      for item in frames)
scaled=[output for frame in frames for output in frame['outputs']
        if output['scale_numerator']==4 and output['scale_denominator']==3
        and output['transform']=='rotate-180']
assert scaled and all(output['texture_cache_bytes']==0 for output in scaled)
canonical=hashlib.sha256(open(sys.argv[3],'rb').read()).hexdigest()
post_vt=hashlib.sha256(open(sys.argv[4],'rb').read()).hexdigest()
assert canonical==post_vt
json.dump({'schema':1,'passed':True,'scale':[4,3],'transform':'rotate-180',
 'canonical_sha256':canonical,'post_vt_sha256':post_vt,
 'vt_release_count':len(releases),'vt_acquire_count':len(acquires),
 'resource_release':{'main_pid_zero':True,'event_fd_closed':True,
  'framebuffer_cleanup':restore['framebuffer_cleanup'],
  'master_drop':restore['master_drop'],'texture_cache_zero':True,'no_fatal':True}},
 open(sys.argv[5],'w'),sort_keys=True)
PY
service_checks=$((service_checks + 1))
systemctl stop gw-uinput-m13.service
[[ $(systemctl show gw-uinput-m13.service -p LoadState --value) == loaded ]]
[[ $(systemctl show gw-uinput-m13.service -p ExecMainStatus --value) == 0 ]]
[[ $(systemctl show gw-uinput-m13.service -p MainPID --value) == 0 ]]
service_checks=$((service_checks + 1))
chvt "$original_vt"
[[ $getty_active_before != active ]] || systemctl start "$getty_unit"
getty_active_after=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_after=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
[[ $getty_active_after == "$getty_active_before" &&
   $getty_enabled_after == "$getty_enabled_before" ]]
systemctl unmask --runtime "$logind_unit" "$logind_socket"
[[ $logind_socket_active_before != active ]] || systemctl start "$logind_socket"
[[ $logind_active_before != active ]] || systemctl start "$logind_unit"
[[ $logind_socket_active_before == active ]] || systemctl stop "$logind_socket"
[[ $logind_active_before == active ]] || systemctl stop "$logind_unit"
logind_active_after=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
logind_socket_active_after=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
logind_enabled_after=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
logind_socket_enabled_after=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
[[ $logind_active_after == "$logind_active_before" &&
   $logind_socket_active_after == "$logind_socket_active_before" &&
   $logind_enabled_after == "$logind_enabled_before" &&
   $logind_socket_enabled_after == "$logind_socket_enabled_before" ]]
"$software/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --expect-restored "$artifact_dir/milestone13-kms-before.json" \
  --output "$artifact_dir/milestone13-kms-after.json"
capture_vt_state "$artifact_dir/milestone13-vt-after.json"
python3 - "$artifact_dir/milestone13-vt-before.json" \
  "$artifact_dir/milestone13-vt-after.json" \
  "$artifact_dir/milestone13-restoration.json" <<'PY'
import json,sys
before,after=(json.load(open(path)) for path in sys.argv[1:3])
checks={key:before[key]==after[key] for key in ('mode','kd','keyboard')}
checks['active_vt']=before['active'][0]==after['active'][0]
assert all(checks.values()),checks
json.dump({'schema':1,'passed':True,'checks':checks},open(sys.argv[3],'w'),sort_keys=True)
PY
python3 - "$artifact_dir/milestone13-getty-state.json" "$getty_unit" \
  "$getty_active_before" "$getty_active_after" "$getty_enabled_before" \
  "$getty_enabled_after" <<'PY'
import json,sys
out,unit,ab,aa,eb,ea=sys.argv[1:]
json.dump({'schema':1,'unit':unit,'active_before':ab,'active_after':aa,
 'enabled_before':eb,'enabled_after':ea,'restored':ab==aa and eb==ea},open(out,'w'),sort_keys=True)
PY
python3 - "$artifact_dir/milestone13-logind-state.json" \
  "$logind_active_before" "$logind_active_after" \
  "$logind_socket_active_before" "$logind_socket_active_after" \
  "$logind_enabled_before" "$logind_enabled_after" \
  "$logind_socket_enabled_before" "$logind_socket_enabled_after" <<'PY'
import json,sys
(out,ab,aa,sb,sa,eb,ea,seb,sea)=sys.argv[1:]
json.dump({'schema':1,'active_before':ab,'active_after':aa,
 'socket_active_before':sb,'socket_active_after':sa,'enabled_before':eb,
 'enabled_after':ea,'socket_enabled_before':seb,'socket_enabled_after':sea,
 'restored':ab==aa and sb==sa and eb==ea and seb==sea},open(out,'w'),sort_keys=True)
PY
result[restoration]=passed
((service_checks == 11))
result[service_results]=passed

failure_stage=evidence
journalctl -u glasswyrmd-m13.service -u gwm-m13.service -u gwcomp-m13.service \
  -u glasswyrmd-m13-gles.service -u gwm-m13-gles.service \
  -u gwcomp-m13-gles.service -b --no-pager \
  >"$artifact_dir/milestone13-session-journal.log"
journalctl -u glasswyrm-m13-drm.service -b --no-pager >>"$artifact_dir/milestone13-session-journal.log"
journalctl -u glasswyrmd-m13.service -u glasswyrmd-m13-compare.service \
  -u glasswyrmd-m13-gles.service -b --no-pager \
  >"$artifact_dir/milestone13-glasswyrmd-journal.log"
journalctl -u gwm-m13.service -u gwm-m13-compare.service \
  -u gwm-m13-gles.service -b --no-pager \
  >"$artifact_dir/milestone13-gwm-journal.log"
journalctl -u gwcomp-m13.service -u gwcomp-m13-compare.service \
  -u gwcomp-m13-gles.service -b --no-pager \
  >"$artifact_dir/milestone13-gwcomp-journal.log"
[[ -s $artifact_dir/milestone13-session-journal.log &&
   -s $artifact_dir/milestone13-glasswyrmd-journal.log &&
   -s $artifact_dir/milestone13-gwm-journal.log &&
   -s $artifact_dir/milestone13-gwcomp-journal.log ]]
result[journal_evidence]=passed
[[ ! -S $runtime/gwm.sock && ! -S $runtime/gwcomp.sock &&
   ! -S $runtime/control.sock && ! -S $runtime/input.sock &&
   ! -S $control/input.sock && ! -S $control/legacy.sock &&
   ! -S /tmp/.X11-unix/X99 ]]
result[socket_cleanup]=passed

failure_stage=archive
evidence=$artifact_dir/evidence
install -d -m 0755 "$evidence"
cp "$artifact_dir"/milestone13-{output-inventory,layout-before,layout-after}.json "$evidence/"
cp "$artifact_dir"/milestone13-{randr-little,randr-big,gw-scale-little,gw-scale-big}.json "$evidence/"
cp "$artifact_dir"/milestone13-{gwinfo-outputs,gwinfo-windows,gwout-result}.json "$evidence/"
cp "$artifact_dir/milestone13-scale-client.json" "$evidence/"
cp "$artifact_dir"/milestone13-{pointer-crossing,sdl-displays}.json "$evidence/"
cp "$artifact_dir/milestone13-fullscreen-outputs.json" "$evidence/"
cp "$artifact_dir/milestone13-frame-sets.jsonl" "$evidence/"
find "$headless/evidence" -type f -name '*.ppm' -exec cp -t "$evidence" -- {} +
"$source_dir/tests/compat/m13/validate_frame_sets.py" \
  "$evidence/milestone13-frame-sets.jsonl" "$evidence"
cp "$artifact_dir/milestone13-renderer-fractional-diff.json" "$evidence/"
cp "$artifact_dir"/milestone13-renderer-{software,gles}.jsonl "$evidence/"
cp "$artifact_dir/milestone13-renderer-drm.jsonl" "$evidence/"
cp "$artifact_dir/milestone13-drm-report.jsonl" "$evidence/"
cp "$artifact_dir/milestone13-drm-representation.json" "$evidence/"
cp "$artifact_dir"/milestone13-{restart,kms-before,kms-after,vt-before,vt-after,restoration}.json "$evidence/"
cp "$artifact_dir"/milestone13-{getty-state,logind-state}.json "$evidence/"
cp "$scenes/scene.jsonl" "$evidence/milestone13-scene.jsonl"
for target in legacy-left legacy-right legacy-spanning-left \
  legacy-spanning-right aware-left aware-right rotate90 flipped; do
  cp "$control_data/milestone13-$target.ppm" "$evidence/"
done
(cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone13-output-scaling-evidence.tar" ./*)
result[archive_validation]=passed
scenario_exit=0 failure_stage=completed
write_facts
trap - EXIT
cleanup
GUEST_SCRIPT
}

milestone13_poll_marker() {
  local marker=$1 guest_pid=$2 output deadline=$((SECONDS + M13_SCREENSHOT_WAIT_SECONDS))
  while ((SECONDS < deadline)); do
    if output=$(guest_run_script 'set -euo pipefail; test -f "$1"; cat "$1"' \
        "$M13_GUEST_CONTROL_DIR/$marker" 2>/dev/null) &&
      grep -Fxq ready <<<"$output" && grep -Fxq mode=1024x768 <<<"$output"; then
      return 0
    fi
    if ! kill -0 "$guest_pid" 2>/dev/null; then
      printf 'M13 guest runtime exited before screenshot marker %s.\n' \
        "$marker" >&2
      return "$M13_GUEST_EXITED_BEFORE_SCREENSHOT"
    fi
    sleep .1
  done
  printf 'Timed out waiting for M13 screenshot marker.\n' >&2
  return 1
}

milestone13_capture_screen() {
  local guest_pid=$1 raw
  milestone13_poll_marker drm-screen-ready "$guest_pid" || return
  raw=$(mktemp "$ARTIFACTS_PATH_ABS/.milestone13-screen.XXXXXX") || return
  virsh --connect "$LIBVIRT_URI" screenshot "$VM_DOMAIN" "$raw" || return
  magick "$raw" -depth 8 "ppm:$ARTIFACTS_PATH_ABS/milestone13-drm-screen.ppm" || return
  rm -f "$raw"
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
    "$ARTIFACTS_PATH_ABS/milestone13-drm-screen.ppm" \
    "$SSH_TARGET:$M13_GUEST_ARTIFACT_DIR/milestone13-drm-screen.ppm" || return
  guest_run_script 'set -euo pipefail; printf "screen-captured\n" >"$1"' \
    "$M13_GUEST_CONTROL_DIR/drm-screen-captured"
}

collect_milestone13_artifacts() {
  local require_complete=${1:-true} name failed=0
  init_artifacts
  for name in "${M13_TEXT_ARTIFACTS[@]}"; do
    [[ $name == milestone13-runtime-test.log ]] && continue
    if [[ $require_complete != true ]] &&
      ! guest_run_script 'set -euo pipefail; test -f "$1"' \
        "$M13_GUEST_ARTIFACT_DIR/$name" 2>/dev/null; then
      continue
    fi
    guest_run_script 'set -euo pipefail; cat "$1"' \
      "$M13_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  for name in "${M13_BINARY_ARTIFACTS[@]}"; do
    [[ $name == milestone13-drm-screen.ppm ]] && continue
    if [[ $require_complete != true ]] &&
      ! guest_run_script 'set -euo pipefail; test -f "$1"' \
        "$M13_GUEST_ARTIFACT_DIR/$name" 2>/dev/null; then
      continue
    fi
    scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
      "$SSH_TARGET:$M13_GUEST_ARTIFACT_DIR/$name" "$ARTIFACTS_PATH_ABS/$name" || failed=1
  done
  return "$failed"
}

write_milestone13_summary() {
  local requested=$1 failure=${2:-}
  local -a command=("$REPO_ROOT/tests/compat/m13/validate_evidence.py" \
    --facts "$ARTIFACTS_PATH_ABS/milestone13-facts.env" \
    --artifact-dir "$ARTIFACTS_PATH_ABS" \
    --output "$ARTIFACTS_PATH_ABS/milestone13-summary.json" \
    --tested-commit "${M13_TESTED_COMMIT:-unknown}" \
    --failure-stage "$failure")
  [[ $requested == true ]] && command+=(--require-pass)
  "${command[@]}"
}

milestone13_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 preflight script capture_status=0
  local collection_required=true
  local drm_device connector target_vt guest_pid=0 guest_status=0
  require_approval milestone13-runtime-test "$approved"
  require_vm_domain
  is_true "$SNAPSHOT_ENABLED" ||
    die 'milestone13-runtime-test requires the configured internal base snapshot.'
  note 'Required release gate: reset; milestone12-runtime-test; reset; milestone13-runtime-test.'
  init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone13_evidence || { status=$?; failure=source-evidence; }
  [[ -n $failure ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z $failure ]]; then
    preflight=$(guest_run_script "$(milestone13_guest_prerequisite_script)" 2>&1) || {
      status=$?; failure=graphical-input-prerequisite;
    }
    printf '%s\n' "$preflight" | tee "$ARTIFACTS_PATH_ABS/milestone13-runtime-test.log"
  fi
  if [[ -z $failure ]]; then
    if verify_milestone13_source_identity && push_source; then :; else
      status=$?; failure=push-source
    fi
  fi
  if [[ -z $failure ]]; then
    drm_device=$(sed -n 's/^drm_primary_node=//p' <<<"$preflight")
    connector=$(sed -n 's/^drm_connector=//p' <<<"$preflight")
    target_vt=$(sed -n 's/^target_vt=//p' <<<"$preflight")
    guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p "$1"' \
      "$M13_GUEST_CONTROL_DIR" || { status=$?; failure=guest-control-reset; }
  fi
  if [[ -z $failure ]]; then
    script=$(milestone13_guest_script)
    guest_run_script "$script" "$GUEST_SOURCE_PATH" "$M13_GUEST_ARTIFACT_DIR" \
      "$drm_device" "$connector" "$target_vt" "$M13_TESTED_COMMIT" \
      >>"$ARTIFACTS_PATH_ABS/milestone13-runtime-test.log" 2>&1 & guest_pid=$!
    if milestone13_capture_screen "$guest_pid"; then :; else
      capture_status=$?
      if ((capture_status == M13_GUEST_EXITED_BEFORE_SCREENSHOT)); then
        if wait "$guest_pid"; then guest_status=1; else guest_status=$?; fi
        guest_pid=0; status=$guest_status; failure=guest-runtime
      else
        status=$capture_status; failure=drm-screenshot
      fi
    fi
    if ((guest_pid)); then
      if wait "$guest_pid"; then :; else
        guest_status=$?
        [[ -n $failure ]] || { status=$guest_status; failure=guest-runtime; }
      fi
    fi
  fi
  [[ -n $failure ]] && collection_required=false
  collect_milestone13_artifacts "$collection_required" || collection=$?
  if ((collection)) && [[ -z $failure ]]; then status=$collection; failure=artifact-collection; fi
  verify_milestone13_source_identity || { [[ -n $failure ]] || failure=source-identity-changed; status=1; }
  if [[ -n $failure ]]; then
    write_milestone13_summary false "$failure" || true
    printf 'Milestone 13 VM runtime test failed during: %s\n' "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  write_milestone13_summary true '' || return
  printf 'Milestone 13 VM runtime test passed.\n'
  print_artifacts
}
