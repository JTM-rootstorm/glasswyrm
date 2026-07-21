#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE14_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE14_LOADED=1

M14_REQUIRED_BASE_COMMIT=6864ea631d61636289a21c7d2d6655a17be0c004
M14_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m14-artifacts
M14_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m14-control
M14_TESTED_COMMIT=
M14_TEXT_ARTIFACTS=(
  milestone14-meson-test.log milestone14-source-layout.log
  milestone14-qxl-capability.json milestone14-qxl-state.json
  milestone14-headless-report.jsonl milestone14-gw-vrr.log
  milestone14-gwout.log milestone14-gwinfo.json
  milestone14-policy-matrix.json milestone14-sdl-vrr.json
  milestone14-sdl-probe.json milestone14-client-build.log
  milestone14-restart.json milestone14-restoration.json
  milestone14-glasswyrmd-journal.log milestone14-gwm-journal.log
  milestone14-gwcomp-journal.log milestone14-facts.env
)
M14_BINARY_ARTIFACTS=(milestone14-vm-vrr-evidence.tar)

milestone14_source_status_ignored() {
  local line=$1
  [[ $line == '?? Plans/'* || $line == '?? .codex/'* ||
     $line == '?? '*'/__pycache__/'*.pyc ]]
}

verify_milestone14_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line ]] || milestone14_source_status_ignored "$line" ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || {
    printf 'Milestone 14 VM acceptance requires committed source outside Plans/.\n%s\n' \
      "$unexpected" >&2
    return 1
  }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  [[ -z $M14_TESTED_COMMIT || $current == "$M14_TESTED_COMMIT" ]]
}

prepare_milestone14_evidence() {
  M14_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_milestone14_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M14_REQUIRED_BASE_COMMIT" "$M14_TESTED_COMMIT" || {
    printf 'HEAD is not based on required Milestone 14 commit %s\n' \
      "$M14_REQUIRED_BASE_COMMIT" >&2
    return 1
  }
}

milestone14_guest_prerequisite_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
[[ -c /dev/uinput && -c /dev/tty1 && -c /dev/tty2 ]] || {
  printf '%s\n' 'M14 QXL requires /dev/uinput and two usable virtual terminals.' >&2
  exit 40
}
shopt -s nullglob
cards=(/dev/dri/card[0-9]*)
((${#cards[@]})) || { printf '%s\n' 'M14 QXL requires a DRM primary node.' >&2; exit 41; }
primary=${cards[0]}; card=${primary##*/}; connector=
for status in /sys/class/drm/"$card"-*/status; do
  [[ $(<"$status") == connected ]] || continue
  grep -Fxq 1024x768 "${status%/status}/modes" || continue
  connector=${status%/status}; connector=${connector##*/}; connector=${connector#"$card"-}
  break
done
[[ -n $connector ]] || {
  printf '%s\n' 'M14 QXL requires a connected exact 1024x768 output.' >&2
  exit 42
}
driver=$(basename "$(readlink -f "/sys/class/drm/$card/device/driver")")
[[ $driver == qxl ]] || {
  printf 'M14 QXL unsupported profile requires driver qxl, found %s.\n' "$driver" >&2
  exit 43
}
printf 'drm_primary_node=%s\ndrm_driver=%s\ndrm_connector=%s\n' \
  "$primary" "$driver" "$connector"
printf 'drm_mode=1024x768\ntarget_vt=/dev/tty2\nuinput_device=/dev/uinput\n'
GUEST_SCRIPT
}

milestone14_guest_script() {
  cat <<'GUEST_SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
source_dir=$1 artifact_dir=$2 drm_device=$3 connector=$4 target_vt=$5 tested_commit=$6
build=/var/tmp/glasswyrm-build-m14
asan=/var/tmp/glasswyrm-build-m14-asan
default=/var/tmp/glasswyrm-build-m14-default
headless=/var/tmp/glasswyrm-build-m14-headless
drm=/var/tmp/glasswyrm-build-m14-drm
clang_build=/var/tmp/glasswyrm-build-m14-clang
components=/var/tmp/glasswyrm-build-m14-components
clients=/var/tmp/glasswyrm-m14-clients
runtime=/run/glasswyrm-m14
control=/var/tmp/glasswyrm-m14-control
work=/var/tmp/glasswyrm-m14-headless
qxl=/var/tmp/glasswyrm-m14-qxl
facts=$artifact_dir/milestone14-facts.env
failure_stage=prerequisite scenario_exit=1 clang=unavailable
declare -A result
required_results=(historical_default strict_m14 strict_gles sanitizer
  component_builds api_consumers source_layout fake_drm_matrix
  simulated_headless_matrix raw_little_big gwout_vrr gwinfo_vrr
  vrr_policy_matrix sdl_vrr_reuse
  qxl_unsupported vt_replay gwm_replay compositor_replay restoration
  socket_cleanup archive_validation journal_evidence)
for key in "${required_results[@]}"; do result[$key]=failed; done
getty_state_captured=false logind_state_captured=false original_vt=
getty_unit='' getty_active_before='' getty_enabled_before=''
logind_unit=systemd-logind.service logind_socket=systemd-logind-varlink.socket
logind_active_before='' logind_socket_active_before=''
logind_enabled_before='' logind_socket_enabled_before=''
client_pid=0 focus_a_pid=0

write_facts() {
  {
    printf 'required_base_commit=6864ea631d61636289a21c7d2d6655a17be0c004\n'
    printf 'tested_commit=%s\nfailure_stage=%s\nscenario_exit=%s\n' \
      "$tested_commit" "$failure_stage" "$scenario_exit"
    printf 'api_version=0.9.0\nsoversion=0\nwire_version=1.0\n'
    printf 'api_consumer_versions=0.1-0.9\nsource_layout_allowlist=empty\n'
    printf 'vm_profile=qxl-unsupported\nsnapshot_name=base\n'
    printf 'headless_outputs=LEFT,RIGHT\nheadless_vrr_ranges=40000-60000,48000-75000\n'
    printf 'compiler_c=%s\ncompiler_cxx=%s\n' \
      "$(cc --version 2>/dev/null | head -n1 || echo unavailable)" \
      "$(c++ --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' \
      "$(meson --version 2>/dev/null || echo unavailable)" \
      "$(ninja --version 2>/dev/null || echo unavailable)" \
      "$(systemctl --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'kernel=%s\nlibdrm=%s\ndrm_driver=qxl\ndrm_connector=%s\n' \
      "$(uname -r)" "$(pkg-config --modversion libdrm 2>/dev/null || echo unavailable)" \
      "$connector"
    printf 'clang=%s\nx_servers_absent=true\ndisplay_manager_absent=true\n' "$clang"
    for key in "${required_results[@]}"; do
      printf '%s=%s\n' "$key" "${result[$key]}"
    done
  } >"$facts"
}

cleanup() {
  local saved_status=$?
  set +e
  ((client_pid == 0)) || { kill "$client_pid" 2>/dev/null; wait "$client_pid" 2>/dev/null; }
  ((focus_a_pid == 0)) || {
    kill "$focus_a_pid" 2>/dev/null
    wait "$focus_a_pid" 2>/dev/null
  }
  systemctl stop glasswyrmd-m14-headless.service gwm-m14-headless.service \
    gwcomp-m14-headless.service glasswyrm-m14-qxl.service \
    glasswyrmd-m14-single.service gwm-m14-single.service \
    gwcomp-m14-single.service \
    gw-uinput-m14.service >/dev/null 2>&1 || true
  if [[ $logind_state_captured == true ]]; then
    systemctl unmask --runtime "$logind_unit" "$logind_socket" >/dev/null 2>&1
    [[ $logind_socket_active_before != active ]] || systemctl start "$logind_socket" >/dev/null 2>&1
    [[ $logind_active_before != active ]] || systemctl start "$logind_unit" >/dev/null 2>&1
    [[ $logind_socket_active_before == active ]] || systemctl stop "$logind_socket" >/dev/null 2>&1
    [[ $logind_active_before == active ]] || systemctl stop "$logind_unit" >/dev/null 2>&1
    [[ $logind_socket_enabled_before != masked-runtime ]] || systemctl mask --runtime "$logind_socket" >/dev/null 2>&1
    [[ $logind_enabled_before != masked-runtime ]] || systemctl mask --runtime "$logind_unit" >/dev/null 2>&1
  fi
  if [[ $getty_state_captured == true ]]; then
    [[ $getty_active_before != active ]] || systemctl start "$getty_unit" >/dev/null 2>&1
    [[ $getty_active_before == active ]] || systemctl stop "$getty_unit" >/dev/null 2>&1
  fi
  [[ -z $original_vt ]] || chvt "$original_vt" >/dev/null 2>&1
  rm -f /tmp/.X11-unix/X98 /tmp/.X11-unix/X99 "$runtime"/*.sock
  write_facts
  return "$saved_status"
}
trap cleanup EXIT

rm -rf -- "$build" "$asan" "$default" "$headless" "$drm" "$clang_build" \
  "$components" "$clients" "$runtime" "$control" "$work" "$qxl" \
  "$artifact_dir"
install -d -m 0755 "$artifact_dir" "$control" "$work" "$qxl"
install -d -m 0700 "$runtime"

failure_stage=dependencies
install -d -m 0755 /etc/portage/package.use
printf 'media-libs/libglvnd X\nmedia-libs/mesa -llvm\n' \
  >/etc/portage/package.use/glasswyrm-m14
emerge --oneshot --noreplace dev-build/meson dev-build/ninja dev-build/cmake \
  dev-vcs/git net-misc/curl app-crypt/gnupg app-misc/jq media-libs/mesa \
  x11-libs/libdrm dev-libs/libinput x11-libs/libxkbcommon \
  x11-misc/xkeyboard-config x11-libs/libX11 x11-libs/libXext \
  x11-libs/libXfixes x11-libs/libXdamage x11-libs/libXrender \
  x11-libs/libXcomposite x11-libs/libXrandr x11-libs/libxcb \
  x11-libs/xcb-util x11-base/xorg-proto
for forbidden in x11-base/xorg-server x11-base/xwayland x11-base/xwayland-run \
  x11-misc/xvfb x11-misc/lightdm x11-misc/sddm gnome-base/gdm gui-apps/greetd; do
  ! qlist -IC "$forbidden" 2>/dev/null | grep -q . || {
    printf 'Forbidden M14 package installed: %s\n' "$forbidden" >&2; exit 1;
  }
done

failure_stage=sdl-acquisition
"$source_dir/tests/compat/m12/acquire_sdl.sh" "$clients/download"
sdl_archive=$clients/download/SDL2-2.32.10.tar.gz
[[ $(sha256sum "$sdl_archive" | awk '{print $1}') == \
  5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 ]]
failure_stage=client-build
"$source_dir/tests/compat/m12/build_clients.sh" "$sdl_archive" \
  "$clients/source" "$clients/build" "$clients/install" \
  >"$artifact_dir/milestone14-client-build.log" 2>&1

failure_stage='build-matrix'
setup_build() {
  local directory=$1; shift
  meson setup "$directory" "$source_dir" --wipe -Dwerror=true "$@"
  meson compile -C "$directory"
}
setup_build "$default" -Dexperimental=false -Drender_gl=false
meson test -C "$default" --print-errorlogs
result[historical_default]=passed
setup_build "$build" -Dexperimental=true -Drender_gl=false \
  -Dheadless_backend=true -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$build" --print-errorlogs | tee "$artifact_dir/milestone14-meson-test.log"
result[strict_m14]=passed
setup_build "$headless" -Dexperimental=true -Drender_gl=true \
  -Dheadless_backend=true -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$headless" --print-errorlogs
result[strict_gles]=passed
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Dasan=true -Dubsan=true \
  -Dexperimental=true -Drender_gl=true -Dheadless_backend=true \
  -Ddrm_backend=true -Dlibinput_backend=true
meson compile -C "$asan" -j1
meson test -C "$asan" --print-errorlogs
result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ setup_build "$clang_build" -Dexperimental=true \
    -Drender_gl=true -Dheadless_backend=true -Ddrm_backend=true \
    -Dlibinput_backend=true
  meson test -C "$clang_build" --print-errorlogs
  clang=passed
fi
setup_build "$drm" -Dexperimental=true -Drender_gl=false \
  -Ddrm_backend=true -Dheadless_backend=false -Dglasswyrmd=false -Dgwm=false

for specification in \
  'server-historical -Dexperimental=false -Dlibgwipc=false -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'server-m14 -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'gwm -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false' \
  'compositor-headless-vrr -Dexperimental=true -Drender_gl=false -Ddrm_backend=false -Dheadless_backend=true -Dglasswyrmd=false -Dgwm=false' \
  'compositor-software -Dexperimental=true -Drender_gl=false -Dglasswyrmd=false -Dgwm=false' \
  'compositor-gles -Dexperimental=true -Drender_gl=true -Dglasswyrmd=false -Dgwm=false' \
  'ipc -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false' \
  'tools -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=true' \
  'session -Dexperimental=true -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false'; do
  read -r -a fields <<<"$specification"; name=${fields[0]}; unset 'fields[0]'
  setup_build "$components/$name" "${fields[@]}"
done
result[component_builds]=passed
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" "$build"
result[api_consumers]=passed
"$source_dir/tests/tools/source_layout_test.sh" | tee "$artifact_dir/milestone14-source-layout.log"
result[source_layout]=passed

failure_stage='focused-matrices'
meson test -C "$build" --print-errorlogs drm-vrr-capability-property \
  drm-vrr-saved-state drm-vrr-timing drm-presenter-vrr-state \
  drm-presenter-vrr-integration
result[fake_drm_matrix]=passed
meson test -C "$build" --print-errorlogs headless-vrr-simulation \
  headless-vrr-presenter gwcomp-vrr-presentation gwm-vrr-process \
  server-vrr-lifecycle output-tools-control-client
result[simulated_headless_matrix]=passed
meson test -C "$build" --print-errorlogs gw-vrr-dispatch gw-vrr-integration \
  >"$artifact_dir/milestone14-gw-vrr.log"
printf '{"schema":1,"little":true,"big":true,"tests":["gw-vrr-dispatch","gw-vrr-integration"]}\n' \
  >>"$artifact_dir/milestone14-gw-vrr.log"
result[raw_little_big]=passed

find_target() {
  local directory=$1 name=$2 path
  path=$(find "$directory" -type f -name "$name" -perm -0100 -print -quit)
  [[ -n $path ]] || { printf 'Missing Meson target %s in %s\n' "$name" "$directory" >&2; return 1; }
  printf '%s\n' "$path"
}
vrr_client=$(find_target "$build" m14_vrr_client)
uinput_helper=$(find_target "$build" gw_uinput_m11)
wait_socket() {
  local path=$1
  for _ in {1..400}; do [[ -S $path ]] && return; sleep .05; done
  printf 'Timed out waiting for M14 socket: %s\n' "$path" >&2; return 1
}
start_headless_compositor() {
  systemd-run --unit=gwcomp-m14-headless --property=Type=simple --no-block -- \
    "$build/src/gwcomp" --backend headless --ipc-socket "$runtime/gwcomp.sock" \
    --dump-dir "$work/frames" --headless-output LEFT:800x600@60000 \
    --headless-output RIGHT:640x480@75000 \
    --headless-vrr LEFT=40000-60000 --headless-vrr RIGHT=48000-75000 \
    --vrr-report "$work/headless-vrr.jsonl"
  wait_socket "$runtime/gwcomp.sock"
}
start_headless_stack() {
  systemd-run --unit=gwm-m14-headless --property=Type=simple --no-block -- \
    "$build/src/gwm" --ipc-socket "$runtime/gwm.sock"
  wait_socket "$runtime/gwm.sock"
  start_headless_compositor
  systemd-run --unit=glasswyrmd-m14-headless --property=Type=simple --no-block -- \
    "$build/src/glasswyrmd" --display 99 --wm-socket "$runtime/gwm.sock" \
    --compositor-socket "$runtime/gwcomp.sock" --software-content \
    --output-model --control-socket "$runtime/control.sock" --game-compat \
    --vrr-protocol
  wait_socket "$runtime/control.sock"; wait_socket /tmp/.X11-unix/X99
}

failure_stage='headless-runtime'
start_headless_stack
"$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
  >"$artifact_dir/milestone14-gwinfo.json"
python3 - "$artifact_dir/milestone14-gwinfo.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); assert len(d['vrr'])==2
assert all(x['simulated'] and not x['hardware_capable'] and
           x['kms_controllable'] for x in d['vrr'])
PY
result[gwinfo_vrr]=passed
declare -A client_mode=([off]=windowed [fullscreen]=fullscreen
  [focused]=windowed [app-requested]=app-requested
  [always-eligible]=windowed)
wait_file() {
  local path=$1
  for _ in {1..400}; do [[ -s $path ]] && return; sleep .05; done
  printf 'Timed out waiting for M14 file: %s\n' "$path" >&2
  return 1
}
wait_policy_cleanup() {
  local state="$work/gwinfo-cleanup.json"
  for _ in {1..400}; do
    if "$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
          >"$state" 2>/dev/null &&
       python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
assert not d.get("windows")
assert d.get("vrr") and all(x.get("candidate_window")==0 for x in d["vrr"])' \
          "$state" 2>/dev/null; then
      rm -f "$state"
      return
    fi
    sleep .05
  done
  printf 'Timed out waiting for coordinated M14 client cleanup\n' >&2
  [[ ! -s $state ]] || cat "$state" >&2
  return 1
}
wait_focus_restore() {
  local state=$1 client_state=$2
  for _ in {1..400}; do
    if "$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
          >"$state" 2>/dev/null &&
       python3 -c 'import json,sys
d=json.load(open(sys.argv[1])); client=json.load(open(sys.argv[2]))
left=[x for x in d.get("vrr",[]) if x.get("name")=="LEFT"]
assert len(left)==1 and left[0].get("candidate_window")==client.get("window")' \
          "$state" "$client_state" 2>/dev/null; then
      return
    fi
    sleep .05
  done
  printf 'Timed out waiting for M14 focus restoration\n' >&2
  [[ ! -s $state ]] || cat "$state" >&2
  return 1
}
wait_restart_state() {
  local state=$1 client_state=$2
  for _ in {1..400}; do
    if "$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
          >"$state" 2>/dev/null &&
       python3 -c 'import json,sys
d=json.load(open(sys.argv[1])); client=json.load(open(sys.argv[2]))
window=client.get("window")
left=[x for x in d.get("vrr",[]) if x.get("name")=="LEFT"]
assert isinstance(window,int) and window!=0 and len(left)==1
state=left[0]
assert state.get("policy")=="focused" and state.get("decision")=="enabled"
assert state.get("desired_enabled") is True
assert state.get("effective_enabled") is True
assert state.get("candidate_window")==window
assert state.get("reasons")==["simulated-headless"]' \
          "$state" "$client_state" 2>/dev/null; then
      return
    fi
    sleep .05
  done
  printf 'Timed out waiting for stable M14 restart state\n' >&2
  [[ ! -s $state ]] || cat "$state" >&2
  return 1
}
run_policy_client() {
  local label=$1 mode=$2 client=$3 preference=${4:-}
  "$build/tools/gwout" --socket "$runtime/control.sock" set LEFT --vrr "$mode" --json \
    >>"$artifact_dir/milestone14-gwout.log"
  local -a arguments=(--display :99 --mode "$client" --hold-ms 700
    --result "$work/client-$label.json")
  [[ -z $preference ]] || arguments+=(--preference "$preference")
  DISPLAY=:99 "$vrr_client" "${arguments[@]}" & client_pid=$!
  wait_file "$work/client-$label.json"
  "$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
    >"$work/gwinfo-$label.json"
  wait "$client_pid"; client_pid=0
  wait_policy_cleanup
}
for mode in off fullscreen focused app-requested always-eligible; do
  run_policy_client "$mode" "$mode" "${client_mode[$mode]}"
done

for preference in default prefer disable; do
  run_policy_client "app-$preference" app-requested windowed "$preference"
done

"$build/tools/gwout" --socket "$runtime/control.sock" set LEFT --vrr focused --json \
  >>"$artifact_dir/milestone14-gwout.log"
DISPLAY=:99 "$vrr_client" --display :99 --mode windowed --hold-ms 3000 \
  --result "$work/client-focus-a.json" & focus_a_pid=$!
wait_file "$work/client-focus-a.json"
DISPLAY=:99 "$vrr_client" --display :99 --mode windowed --hold-ms 700 \
  --result "$work/client-focus-b.json" & focus_b=$!
client_pid=$focus_b
wait_file "$work/client-focus-b.json"
"$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
  >"$work/gwinfo-focus-b.json"
wait "$focus_b"; client_pid=0
wait_focus_restore "$work/gwinfo-focus-a.json" "$work/client-focus-a.json"
wait "$focus_a_pid"; focus_a_pid=0
wait_policy_cleanup

python3 - "$artifact_dir/milestone14-gwout.log" <<'PY'
import json,sys
records=[json.loads(x) for x in open(sys.argv[1]) if x.strip()]
assert len(records)==9
assert all(r.get('acknowledgement',{}).get('result')==1 for r in records)
PY
result[gwout_vrr]=passed

"$build/tools/gwout" --socket "$runtime/control.sock" set LEFT --vrr focused --json \
  >>"$artifact_dir/milestone14-gwout.log"
DISPLAY=:99 "$vrr_client" --display :99 --mode windowed --hold-ms 8000 \
  --result "$work/client-restart.json" & client_pid=$!
wait_file "$work/client-restart.json"
wait_restart_state "$work/pre-restart.json" "$work/client-restart.json"
systemctl restart gwm-m14-headless.service
wait_socket "$runtime/gwm.sock"
wait_restart_state "$work/post-gwm.json" "$work/client-restart.json"
result[gwm_replay]=passed
mv "$work/headless-vrr.jsonl" "$work/headless-vrr-before-restart.jsonl"
systemctl restart gwcomp-m14-headless.service
wait_socket "$runtime/gwcomp.sock"
wait_restart_state "$work/post-gwcomp.json" "$work/client-restart.json"
result[compositor_replay]=passed
python3 - "$work/pre-restart.json" "$work/post-gwm.json" \
  "$work/post-gwcomp.json" "$work/client-restart.json" \
  "$artifact_dir/milestone14-restart.json" <<'PY'
import json,sys
before,gwm,comp,client=(json.load(open(p)) for p in sys.argv[1:5])
window=client['window']
def left(value): return next(x for x in value['vrr'] if x['name']=='LEFT')
def semantic(value):
 x=left(value)
 return (x['policy'],x['decision'],x['desired_enabled'],x['effective_enabled'],
         x['candidate_window'],tuple(x['reasons']))
expected=semantic(before)
assert expected[0:5]==('focused','enabled',True,True,window)
assert semantic(gwm)==expected and semantic(comp)==expected
json.dump({'schema':1,'passed':True,'gwm_replay':True,
 'compositor_replay':True,'candidate_window':window,
 'semantic_state':{'policy':expected[0],'decision':expected[1],
 'desired_enabled':expected[2],'effective_enabled':expected[3],
 'reasons':list(expected[5])}},open(sys.argv[5],'w'),sort_keys=True)
PY
wait "$client_pid"; client_pid=0
systemctl stop glasswyrmd-m14-headless.service gwm-m14-headless.service \
  gwcomp-m14-headless.service

failure_stage='single-output-acceptance'
systemd-run --unit=gwm-m14-single --property=Type=simple --no-block -- \
  "$build/src/gwm" --ipc-socket "$runtime/gwm-single.sock"
wait_socket "$runtime/gwm-single.sock"
systemd-run --unit=gwcomp-m14-single --property=Type=simple --no-block -- \
  "$build/src/gwcomp" --backend headless \
  --ipc-socket "$runtime/gwcomp-single.sock" --dump-dir "$work/single-frames" \
  --headless-output LEFT:800x600@60000 --headless-vrr LEFT=40000-60000 \
  --vrr-report "$work/headless-vrr-single.jsonl"
wait_socket "$runtime/gwcomp-single.sock"
systemd-run --unit=glasswyrmd-m14-single --property=Type=simple --no-block -- \
  "$build/src/glasswyrmd" --display 98 --wm-socket "$runtime/gwm-single.sock" \
  --compositor-socket "$runtime/gwcomp-single.sock" --software-content \
  --output-model --control-socket "$runtime/control-single.sock" \
  --game-compat --vrr-protocol
wait_socket "$runtime/control-single.sock"; wait_socket /tmp/.X11-unix/X98

"$build/tools/gwout" --socket "$runtime/control-single.sock" set LEFT \
  --vrr fullscreen --json >/dev/null
DISPLAY=:98 "$vrr_client" --display :98 --mode borderless --hold-ms 700 \
  --result "$work/client-borderless.json" & client_pid=$!
wait_file "$work/client-borderless.json"
"$build/tools/gwinfo" --socket "$runtime/control-single.sock" vrr --json \
  >"$work/gwinfo-borderless.json"
wait "$client_pid"; client_pid=0

sdl_library="$clients/install/lib64:$clients/install/lib"
SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
  LD_LIBRARY_PATH="$sdl_library" DISPLAY=:98 \
  "$clients/install/bin/m12_sdl_probe" \
  --output "$artifact_dir/milestone14-sdl-probe.json"
"$build/tools/gwout" --socket "$runtime/control-single.sock" set LEFT \
  --vrr app-requested --json >/dev/null
SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
  LD_LIBRARY_PATH="$sdl_library" DISPLAY=:98 \
  "$clients/install/bin/m12_sdl_probe" --output "$work/sdl-app-requested.json"

python3 - "$work/gwinfo-off.json" "$work/gwinfo-fullscreen.json" \
  "$work/gwinfo-focused.json" "$work/gwinfo-app-requested.json" \
  "$work/gwinfo-always-eligible.json" "$work/gwinfo-app-default.json" \
  "$work/gwinfo-app-prefer.json" "$work/gwinfo-app-disable.json" \
  "$work/gwinfo-focus-b.json" "$work/gwinfo-focus-a.json" \
  "$work/client-focus-b.json" "$work/client-focus-a.json" \
  "$work/gwinfo-borderless.json" \
  "$artifact_dir/milestone14-policy-matrix.json" <<'PY'
import json,sys
docs=[json.load(open(p)) for p in sys.argv[1:11]]
focus_b,focus_a=(json.load(open(p)) for p in sys.argv[11:13])
borderless=json.load(open(sys.argv[13])); destination=sys.argv[14]
def left(value): return next(x for x in value['vrr'] if x['name']=='LEFT')
expected=(('off',False,False,'policy-off'),('fullscreen',True,True,None),
 ('focused',True,True,None),('app-requested',True,True,None),
 ('always-eligible',True,True,'manual-always-eligible'))
modes={}
for value,(policy,desired,effective,reason) in zip(docs[:5],expected):
 out=left(value); assert out['policy']==policy
 assert out['desired_enabled'] is desired and out['effective_enabled'] is effective
 if reason: assert reason in out['reasons']
 modes[policy]={'desired_enabled':desired,'effective_enabled':effective,
                'candidate_window':out['candidate_window'],'reasons':out['reasons']}
transitions=[]
for value,preference,effective,reason in zip(docs[5:8],('default','prefer','disable'),
 (False,True,False),('window-did-not-request',None,'window-preference-disabled')):
 out=left(value); assert out['policy']=='app-requested' and out['effective_enabled'] is effective
 window=next(x for x in value['windows'] if x['output']==out['id'])
 assert window['preference']==preference
 if reason:
  assert 'no-candidate' in out['reasons']
  assert reason in window['reasons']
 transitions.append({'preference':preference,'effective_enabled':effective,
                     'output_reasons':out['reasons'],
                     'window_reasons':window['reasons']})
b=left(docs[8]); a=left(docs[9])
assert b['candidate_window']==focus_b['window'] and a['candidate_window']==focus_a['window']
bo=left(borderless); bw=next(x for x in borderless['windows'] if x['window']==bo['candidate_window'])
assert bo['effective_enabled'] and bw['borderless_fullscreen']
json.dump({'schema':1,'passed':True,'modes':modes,
 'app_requested_transitions':transitions,
 'focused_candidates':[b['candidate_window'],a['candidate_window']],
 'borderless':{'effective_enabled':True,'classified':True,
               'candidate_window':bo['candidate_window']}},open(destination,'w'),sort_keys=True)
PY
result[vrr_policy_matrix]=passed

python3 - "$work/headless-vrr-single.jsonl" \
  "$artifact_dir/milestone14-sdl-probe.json" "$work/sdl-app-requested.json" \
  "$artifact_dir/milestone14-sdl-vrr.json" <<'PY'
import json,sys
records=[json.loads(x) for x in open(sys.argv[1]) if x.strip()]
first=json.load(open(sys.argv[2])); second=json.load(open(sys.argv[3]))
assert first['passed'] and second['passed']
assert first['probe']==second['probe']=='m12_sdl_probe'
assert first['sdl_version']==second['sdl_version']=='2.32.10'
assert first['video_driver']==second['video_driver']=='x11'
decisions=[x for x in records if x.get('record')=='decision']
fullscreen=[x for x in decisions if x.get('policy_mode')==2]
requested=[x for x in decisions if x.get('policy_mode')==4]
enabled_index=next((i for i,x in enumerate(fullscreen)
                    if x.get('effective_enabled') and
                    x.get('candidate_window_id',0)!=0),None)
assert enabled_index is not None
assert any(i>enabled_index and not x.get('effective_enabled') and
           'NoCandidate' in x.get('reason_names',())
           for i,x in enumerate(fullscreen))
assert requested and all(not x.get('effective_enabled') and
                         'NoCandidate' in x.get('reason_names',())
                         for x in requested)
json.dump({'schema':1,'passed':True,'sdl_version':'2.32.10',
 'fullscreen_desktop_enabled':True,'borderless_windowed_rejected':True,
 'implicit_app_request':False,'app_requested_effective':False},
 open(sys.argv[4],'w'),sort_keys=True)
PY
result[sdl_vrr_reuse]=passed
systemctl stop glasswyrmd-m14-single.service gwcomp-m14-single.service \
  gwm-m14-single.service

cat "$work/headless-vrr-before-restart.jsonl" "$work/headless-vrr.jsonl" \
  "$work/headless-vrr-single.jsonl" \
  >"$artifact_dir/milestone14-headless-report.jsonl"

failure_stage='qxl-runtime'
rm -rf -- "$runtime"; install -d -m 0700 "$runtime"
systemd-run --unit=gw-uinput-m14.service --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property='DeviceAllow=/dev/uinput rw' \
  --no-block -- "$uinput_helper" serve --control-socket "$control/input.sock" \
  --devices-json "$control/devices.json"
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
fd=os.open(sys.argv[1],os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
 s=bytearray(6); m=bytearray(8); kd=bytearray(4); kb=bytearray(4)
 fcntl.ioctl(fd,0x5603,s,True); fcntl.ioctl(fd,0x5601,m,True)
 fcntl.ioctl(fd,0x4B3B,kd,True); fcntl.ioctl(fd,0x4B44,kb,True)
finally: os.close(fd)
json.dump({'active':list(struct.unpack('=HHH',s)),'mode':list(struct.unpack('=BBhhh',m)),
 'kd':struct.unpack('=i',kd)[0],'keyboard':struct.unpack('=i',kb)[0]},open(sys.argv[2],'w'),sort_keys=True)
PY
}
original_vt=$(python3 - /dev/tty0 <<'PY'
import fcntl,os,struct,sys
fd=os.open(sys.argv[1],os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC); s=bytearray(6)
try: fcntl.ioctl(fd,0x5603,s,True)
finally: os.close(fd)
print(struct.unpack('=HHH',s)[0])
PY
)
getty_unit=getty@${target_vt##*/}.service
getty_active_before=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_before=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
logind_active_before=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
logind_socket_active_before=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
logind_enabled_before=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
logind_socket_enabled_before=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
getty_state_captured=true logind_state_captured=true
"$build/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --snapshot-state --output "$qxl/kms-before.json"
capture_vt_state "$qxl/vt-before.json"
[[ $getty_active_before != active ]] || systemctl stop "$getty_unit"
systemctl mask --runtime --now "$logind_socket" "$logind_unit"
systemd-run --unit=glasswyrm-m14-qxl --property=Type=simple \
  --setenv="PATH=$build/src:/usr/bin:/bin" --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property="DeviceAllow=$drm_device rw" \
  --property="DeviceAllow=$target_vt rw" --property="DeviceAllow=$keyboard r" \
  --property="DeviceAllow=$pointer r" --property=StandardInput=tty-force \
  --property="TTYPath=$target_vt" --property=StandardOutput=journal \
  --property=StandardError=journal --property=TTYReset=yes \
  --property=TTYVHangup=yes --property=TTYVTDisallocate=no \
  --property=KillMode=mixed --property=SuccessExitStatus=143 --no-block -- \
  "$build/src/glasswyrm-session" --runtime-dir "$runtime" --display 99 \
  --backend drm --drm-device "$drm_device" --tty "$target_vt" \
  --connector "$connector" --mode 1024x768 --drm-api auto \
  --input-device "$keyboard" --input-device "$pointer" --output-model \
  --control-socket "$runtime/control.sock" --vrr-protocol --renderer software \
  --mirror-dump-dir "$qxl/frames" --drm-report "$qxl/drm-report.jsonl" \
  --vrr-report "$qxl/vrr-report.jsonl"
wait_socket "$runtime/control.sock"; wait_socket /tmp/.X11-unix/X99
set +e
"$build/tools/gwout" --socket "$runtime/control.sock" set "$connector" \
  --vrr always-eligible --json >"$qxl/gwout-rejected.stdout" \
  2>"$qxl/gwout-rejected.stderr"
qxl_gwout_status=$?
set -e
((qxl_gwout_status != 0))
grep -Fxq 'gwout: selected output does not provide controllable VRR' \
  "$qxl/gwout-rejected.stderr"
python3 - "$qxl_gwout_status" "$qxl/gwout-rejected.stderr" \
  "$artifact_dir/milestone14-gwout.log" <<'PY'
import json,sys
status=int(sys.argv[1])
diagnostics=[line for line in open(sys.argv[2]).read().splitlines()
             if line.startswith('gwout:')]
expected='gwout: selected output does not provide controllable VRR'
assert status!=0 and diagnostics==[expected]
with open(sys.argv[3],'a') as output:
 json.dump({'profile':'qxl-unsupported','requested_policy':'always-eligible',
            'accepted':False,'exit_status':status,'error':expected},output,sort_keys=True)
 output.write('\n')
PY
"$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json \
  >"$artifact_dir/milestone14-qxl-state.json"
python3 - "$qxl/vrr-report.jsonl" "$artifact_dir/milestone14-qxl-capability.json" \
  "$artifact_dir/milestone14-qxl-state.json" <<'PY'
import json,pathlib,sys,time
report=pathlib.Path(sys.argv[1]); deadline=time.monotonic()+10
while True:
 try:
  records=[json.loads(x) for x in report.read_text().splitlines() if x.strip()]
  capability=next(x for x in records if x.get('record')=='vrr-capability')
  break
 except (FileNotFoundError,StopIteration,json.JSONDecodeError):
  if time.monotonic()>=deadline: raise SystemExit('QXL VRR capability record was not observed')
  time.sleep(.05)
assert not capability['connector_property_present']
assert not capability['connector_property_value']
assert capability['crtc_property_present'] and capability['crtc_property_id'] > 0
assert not capability['controllable'] and not capability['atomic_test_on']
capability.update({'schema':1,'profile':'qxl-unsupported','passed':True})
json.dump(capability,open(sys.argv[2],'w'),sort_keys=True)
state=json.load(open(sys.argv[3])); assert len(state['vrr'])==1
v=state['vrr'][0]
assert not v['simulated'] and not v['hardware_capable'] and not v['kms_controllable']
assert v['policy']=='off' and not v.get('effective_enabled',False)
assert {'output-not-vrr-capable','vrr-property-missing'} <= set(v['reasons'])
PY
result[qxl_unsupported]=passed

chvt 1
python3 - "$qxl/drm-report.jsonl" release <<'PY'
import json,pathlib,sys,time
p=pathlib.Path(sys.argv[1]); deadline=time.monotonic()+10
while True:
 try:
  if any(x.get('record')=='vt' and x.get('transition')==sys.argv[2]
         for x in map(json.loads,p.read_text().splitlines())): break
 except (FileNotFoundError,json.JSONDecodeError): pass
 if time.monotonic()>=deadline: raise SystemExit('M14 VT release was not observed')
 time.sleep(.05)
PY
chvt "${target_vt#/dev/tty}"
python3 - "$qxl/drm-report.jsonl" acquire <<'PY'
import json,pathlib,sys,time
p=pathlib.Path(sys.argv[1]); deadline=time.monotonic()+10
while True:
 try:
  if any(x.get('record')=='vt' and x.get('transition')==sys.argv[2] and
         x.get('master_owned') is True for x in map(json.loads,p.read_text().splitlines())): break
 except (FileNotFoundError,json.JSONDecodeError): pass
 if time.monotonic()>=deadline: raise SystemExit('M14 VT acquire was not observed')
 time.sleep(.05)
PY
"$build/tools/gwinfo" --socket "$runtime/control.sock" vrr --json >"$qxl/post-vt.json"
python3 - "$qxl/post-vt.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))['vrr'][0]; assert not v.get('effective_enabled',False)
PY
result[vt_replay]=passed
systemctl stop glasswyrm-m14-qxl.service gw-uinput-m14.service
chvt "$original_vt"
[[ $getty_active_before != active ]] || systemctl start "$getty_unit"
systemctl unmask --runtime "$logind_unit" "$logind_socket"
[[ $logind_socket_active_before != active ]] || systemctl start "$logind_socket"
[[ $logind_active_before != active ]] || systemctl start "$logind_unit"
[[ $logind_socket_active_before == active ]] || systemctl stop "$logind_socket"
[[ $logind_active_before == active ]] || systemctl stop "$logind_unit"
[[ $logind_socket_enabled_before != masked-runtime ]] || systemctl mask --runtime "$logind_socket"
[[ $logind_enabled_before != masked-runtime ]] || systemctl mask --runtime "$logind_unit"
"$build/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --expect-restored "$qxl/kms-before.json" \
  --output "$qxl/kms-after.json"
capture_vt_state "$qxl/vt-after.json"
getty_active_after=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_after=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
logind_active_after=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
logind_socket_active_after=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
logind_enabled_after=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
logind_socket_enabled_after=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
python3 - "$qxl/kms-before.json" "$qxl/kms-after.json" "$qxl/vt-before.json" \
  "$qxl/vt-after.json" "$qxl/vrr-report.jsonl" \
  "$artifact_dir/milestone14-restoration.json" \
  "$getty_active_before" "$getty_active_after" "$getty_enabled_before" \
  "$getty_enabled_after" "$logind_active_before" "$logind_active_after" \
  "$logind_socket_active_before" "$logind_socket_active_after" \
  "$logind_enabled_before" "$logind_enabled_after" \
  "$logind_socket_enabled_before" "$logind_socket_enabled_after" <<'PY'
import json,sys
kb,ka,vb,va=(json.load(open(x)) for x in sys.argv[1:5])
records=[json.loads(x) for x in open(sys.argv[5]) if x.strip()]
restore=next(x for x in reversed(records) if x.get('record')=='vrr-restore')
states=sys.argv[7:]
vt_restored=(vb['active'][0]==va['active'][0] and
             all(vb[key]==va[key] for key in ('mode','kd','keyboard')))
checks={'kms':kb==ka,'vt':vt_restored,'vrr':restore['readback_success'] and
        restore['original_enabled']==restore['restored_enabled'] and
        restore['kms_restore'] and restore['vt_restore'] and restore['getty_restore'],
        'getty':states[0]==states[1] and states[2]==states[3],
        'logind':states[4]==states[5] and states[6]==states[7] and
                 states[8]==states[9] and states[10]==states[11]}
assert all(checks.values()),checks
json.dump({'schema':1,'passed':True,'checks':checks},open(sys.argv[6],'w'),sort_keys=True)
PY
result[restoration]=passed

failure_stage=evidence
journalctl -u glasswyrmd-m14-headless.service -u glasswyrmd-m14-single.service \
  -u glasswyrm-m14-qxl.service \
  -b --no-pager >"$artifact_dir/milestone14-glasswyrmd-journal.log"
journalctl -u gwm-m14-headless.service -u gwm-m14-single.service \
  -u glasswyrm-m14-qxl.service \
  -b --no-pager >"$artifact_dir/milestone14-gwm-journal.log"
journalctl -u gwcomp-m14-headless.service -u gwcomp-m14-single.service \
  -u glasswyrm-m14-qxl.service \
  -b --no-pager >"$artifact_dir/milestone14-gwcomp-journal.log"
[[ -s $artifact_dir/milestone14-glasswyrmd-journal.log &&
   -s $artifact_dir/milestone14-gwm-journal.log &&
   -s $artifact_dir/milestone14-gwcomp-journal.log ]]
result[journal_evidence]=passed
[[ ! -S $runtime/gwm.sock && ! -S $runtime/gwcomp.sock &&
   ! -S $runtime/control.sock && ! -S $runtime/gwm-single.sock &&
   ! -S $runtime/gwcomp-single.sock && ! -S $runtime/control-single.sock &&
   ! -S /tmp/.X11-unix/X98 && ! -S /tmp/.X11-unix/X99 ]]
result[socket_cleanup]=passed

failure_stage=archive
evidence=$artifact_dir/evidence
install -d -m 0755 "$evidence"
for name in milestone14-qxl-capability.json milestone14-qxl-state.json \
  milestone14-headless-report.jsonl milestone14-gw-vrr.log \
  milestone14-gwout.log milestone14-gwinfo.json milestone14-restart.json \
  milestone14-policy-matrix.json milestone14-sdl-vrr.json \
  milestone14-sdl-probe.json milestone14-restoration.json; do
  cp "$artifact_dir/$name" "$evidence/"
done
(cd "$evidence" && sha256sum -- * >SHA256SUMS && \
  sha256sum --check --status SHA256SUMS && \
  tar -cf "$artifact_dir/milestone14-vm-vrr-evidence.tar" ./*)
result[archive_validation]=passed
scenario_exit=0 failure_stage=completed
write_facts
trap - EXIT
cleanup
GUEST_SCRIPT
}

collect_milestone14_artifacts() {
  local require_complete=${1:-true} name failed=0
  init_artifacts
  for name in "${M14_TEXT_ARTIFACTS[@]}"; do
    if [[ $require_complete != true ]] &&
      ! guest_run_script 'set -euo pipefail; test -f "$1"' \
        "$M14_GUEST_ARTIFACT_DIR/$name" 2>/dev/null; then continue; fi
    guest_run_script 'set -euo pipefail; cat "$1"' \
      "$M14_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  for name in "${M14_BINARY_ARTIFACTS[@]}"; do
    if [[ $require_complete != true ]] &&
      ! guest_run_script 'set -euo pipefail; test -f "$1"' \
        "$M14_GUEST_ARTIFACT_DIR/$name" 2>/dev/null; then continue; fi
    scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
      "$SSH_TARGET:$M14_GUEST_ARTIFACT_DIR/$name" "$ARTIFACTS_PATH_ABS/$name" || failed=1
  done
  return "$failed"
}

write_milestone14_summary() {
  local requested=$1 failure=${2:-}
  local -a command=("$REPO_ROOT/tests/compat/m14/validate_vm_evidence.py"
    --facts "$ARTIFACTS_PATH_ABS/milestone14-facts.env"
    --artifact-dir "$ARTIFACTS_PATH_ABS"
    --output "$ARTIFACTS_PATH_ABS/milestone14-summary.json"
    --tested-commit "${M14_TESTED_COMMIT:-unknown}" --failure-stage "$failure")
  [[ $requested == true ]] && command+=(--require-pass)
  PYTHONDONTWRITEBYTECODE=1 "${command[@]}"
}

milestone14_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 preflight script
  local collection_required=true drm_device connector target_vt
  require_approval milestone14-runtime-test "$approved"
  require_vm_domain
  is_true "$SNAPSHOT_ENABLED" ||
    die 'milestone14-runtime-test requires the configured internal base snapshot.'
  [[ $SNAPSHOT_NAME == base ]] ||
    die "milestone14-runtime-test requires the internal snapshot name 'base'."
  note 'Required release gate: reset; milestone13-runtime-test; reset; milestone14-runtime-test.'
  init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone14_evidence || { status=$?; failure=source-evidence; }
  [[ -n $failure ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z $failure ]]; then
    preflight=$(guest_run_script "$(milestone14_guest_prerequisite_script)" 2>&1) || {
      status=$?; failure=qxl-prerequisite;
    }
    printf '%s\n' "$preflight" | tee "$ARTIFACTS_PATH_ABS/milestone14-runtime-test.log"
  fi
  if [[ -z $failure ]]; then
    if verify_milestone14_source_identity && push_source; then :; else
      status=$?; failure=push-source;
    fi
  fi
  if [[ -z $failure ]]; then
    drm_device=$(sed -n 's/^drm_primary_node=//p' <<<"$preflight")
    connector=$(sed -n 's/^drm_connector=//p' <<<"$preflight")
    target_vt=$(sed -n 's/^target_vt=//p' <<<"$preflight")
    script=$(milestone14_guest_script)
    guest_run_script "$script" "$GUEST_SOURCE_PATH" "$M14_GUEST_ARTIFACT_DIR" \
      "$drm_device" "$connector" "$target_vt" "$M14_TESTED_COMMIT" \
      >>"$ARTIFACTS_PATH_ABS/milestone14-runtime-test.log" 2>&1 || {
        status=$?; failure=guest-runtime;
      }
  fi
  [[ -n $failure ]] && collection_required=false
  collect_milestone14_artifacts "$collection_required" || collection=$?
  if ((collection)) && [[ -z $failure ]]; then
    status=$collection; failure=artifact-collection
  fi
  verify_milestone14_source_identity || {
    [[ -n $failure ]] || failure=source-identity-changed; status=1;
  }
  if [[ -n $failure ]]; then
    write_milestone14_summary false "$failure" || true
    printf 'Milestone 14 VM runtime test failed during: %s\n' "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  write_milestone14_summary true '' || return
  printf 'Milestone 14 VM runtime test passed.\n'
  print_artifacts
}
