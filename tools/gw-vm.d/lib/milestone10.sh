#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE10_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE10_LOADED=1

M10_REQUIRED_BASE_COMMIT=fe0faab39f7a6d28157ee6b96a4f6292a0b7984e
M10_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m10-artifacts
M10_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m10-control
M10_SCREENSHOT_WAIT_SECONDS=1800
M10_TESTED_COMMIT=
M10_TEXT_ARTIFACTS=(milestone10-runtime-test.log milestone10-meson-test.log
  milestone10-drm-probe.json milestone10-drm-report.jsonl
  milestone10-kms-before.json milestone10-kms-active.json milestone10-kms-after.json
  milestone10-vt-before.json milestone10-vt-active.json milestone10-vt-after.json
  milestone10-apps.log milestone10-screenshot-validation.log
  milestone10-glasswyrmd-journal.log milestone10-gwm-journal.log
  milestone10-gwcomp-journal.log milestone10-facts.env)
M10_BINARY_ARTIFACTS=(milestone10-screen.ppm milestone10-screen-after-vt.ppm
  milestone10-drm-evidence.tar)

milestone10_guest_prerequisite_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
shopt -s nullglob
nodes=(/dev/dri/card[0-9]*)
if ((${#nodes[@]} == 0)); then
  printf '%s\n' 'M10 prerequisite failed before package installation: no DRM primary node (/dev/dri/card*) is exposed by the current guest kernel.' >&2
  printf '%s\n' 'Configure the libvirt video device and its virtual GPU DRM driver in the clean snapshot; the M10 harness will never install or rebuild a kernel.' >&2
  exit 20
fi
primary=
for node in "${nodes[@]}"; do
  [[ -c $node && ${node##*/} =~ ^card[0-9]+$ ]] || continue
  primary=$node
  break
done
[[ -n $primary ]] || { printf '%s\n' 'M10 prerequisite failed before package installation: /dev/dri contains no usable DRM primary character device.' >&2; exit 21; }
card=${primary##*/}
driver_link=/sys/class/drm/$card/device/driver
[[ -L $driver_link ]] || { printf 'M10 prerequisite failed before package installation: %s has no bound kernel graphics driver.\n' "$primary" >&2; exit 22; }
driver=$(basename "$(readlink -f "$driver_link")")
[[ -n $driver ]] || { printf 'M10 prerequisite failed before package installation: cannot identify the kernel driver for %s.\n' "$primary" >&2; exit 23; }
connector= modes=() connected=()
for status in /sys/class/drm/"$card"-*/status; do
  [[ $(<"$status") == connected ]] || continue
  name=${status%/status}; name=${name##*/}; name=${name#"$card"-}
  connected+=("$name")
  while IFS= read -r mode; do
    [[ -n $mode ]] && modes+=("$name:$mode")
    if [[ $mode == 1024x768 && -z $connector ]]; then connector=$name; fi
  done <"${status%/status}/modes"
done
[[ -n $connector ]] || {
  printf 'M10 prerequisite failed before package installation: %s (%s) has no connected connector exposing exact mode 1024x768.\n' "$primary" "$driver" >&2
  exit 24
}
for tty in /dev/tty1 /dev/tty2; do
  [[ -c $tty ]] || { printf 'M10 prerequisite failed before package installation: required virtual terminal %s is unavailable.\n' "$tty" >&2; exit 25; }
done
printf 'drm_primary_node=%s\n' "$primary"
printf 'drm_driver=%s\n' "$driver"
printf 'drm_connector=%s\n' "$connector"
printf 'drm_connectors=%s\n' "$(IFS=,; echo "${connected[*]}")"
printf 'drm_modes=%s\n' "$(IFS=,; echo "${modes[*]}")"
printf 'drm_mode=1024x768\n'
printf 'target_vt=/dev/tty2\n'
printf 'virtual_terminals=/dev/tty1,/dev/tty2\n'
GUEST_SCRIPT
}

milestone10_doctor() {
  local failed=0 xml video graphics state probe
  command_exists virsh || return 1
  if xml=$(virsh --connect "$LIBVIRT_URI" dumpxml "$VM_DOMAIN" 2>/dev/null); then
    video=$(sed -n "s/.*<model[^>]*type=['\"]\([^'\"]*\)['\"].*/\1/p" <<<"$xml" | head -n1)
    graphics=$(sed -n "s/.*<graphics[^>]*type=['\"]\([^'\"]*\)['\"].*/\1/p" <<<"$xml" | head -n1)
    if [[ -n $video ]]; then printf '[ok] libvirt video model: %s\n' "$video"; else printf '[missing] libvirt video model\n'; failed=1; fi
    if [[ -n $graphics ]]; then printf '[ok] libvirt graphics type: %s\n' "$graphics"; else printf '[missing] libvirt graphics console\n'; failed=1; fi
  else
    printf '[failed] unable to inspect libvirt domain XML\n'; failed=1
  fi
  if virsh --connect "$LIBVIRT_URI" help screenshot >/dev/null 2>&1; then
    printf '[ok] libvirt screenshot capability\n'
  else
    printf '[missing] libvirt screenshot capability\n'; failed=1
  fi
  state=$(LC_ALL=C virsh --connect "$LIBVIRT_URI" domstate "$VM_DOMAIN" 2>/dev/null || true)
  if [[ $state == running || $state == idle || $state == paused ]]; then
    if probe=$(guest_run_script "$(milestone10_guest_prerequisite_script)" 2>&1); then
      while IFS= read -r line; do printf '[ok] guest %s\n' "$line"; done <<<"$probe"
      printf '[info] guest dumb-buffer and atomic capabilities are confirmed by gw_drm_probe before modeset\n'
    else
      printf '[failed] guest graphical prerequisite probe\n%s\n' "$probe"; failed=1
    fi
  else
    printf '[info] guest DRM, connector, mode, atomic, and VT checks require a running domain\n'
  fi
  return "$failed"
}

verify_milestone10_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line || $line == '?? Plans/'* || $line == '?? .codex/'* ]] || unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || { printf 'Milestone 10 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  [[ -z $M10_TESTED_COMMIT || $current == "$M10_TESTED_COMMIT" ]]
}

prepare_milestone10_evidence() {
  M10_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_milestone10_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M10_REQUIRED_BASE_COMMIT" "$M10_TESTED_COMMIT" || {
    printf 'HEAD is not based on required Milestone 10 commit %s\n' "$M10_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone10-*
}

milestone10_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2 drm_device=$3 connector=$4 target_vt=$5
build=/var/tmp/glasswyrm-build-m10
asan=/var/tmp/glasswyrm-build-m10-asan
runtime=/var/tmp/glasswyrm-build-m10-runtime
drm_only=/var/tmp/glasswyrm-build-m10-drm-only
headless=/var/tmp/glasswyrm-build-m10-headless
server=/var/tmp/glasswyrm-build-m10-server
gwm_build=/var/tmp/glasswyrm-build-m10-gwm
ipc_only=/var/tmp/glasswyrm-build-m10-ipc-only
client_dir=/var/tmp/glasswyrm-m10-clients
dumps=/var/tmp/glasswyrm-m10-dumps scenes=/var/tmp/glasswyrm-m10-scenes
drm_dir=/var/tmp/glasswyrm-m10-drm control=/var/tmp/glasswyrm-m10-control
facts=$artifact_dir/milestone10-facts.env runtime_log=$artifact_dir/milestone10-runtime-test.log
failure_stage=kernel-prerequisite scenario_exit=1 getty_was_active=false clients_pid=
getty_unit=getty@${target_vt##*/}.service getty_state_captured=false
getty_active_before=unknown getty_enabled_before=unknown
getty_active_after=unknown getty_enabled_after=unknown
results=(strict_tests source_layout_audit source_layout_budget refactor_parity sanitizer clang
 headless_no_libdrm dual_backend drm_only historical_components m4_m9_regressions
 m9_golden_mirror initial_modeset page_flip delayed_ack delayed_release hash_parity screenshot_equal
 vt_release vt_acquire remodeset post_vt_repaint post_vt_screenshot_equal kms_restore kd_restore
 vt_mode_restore active_vt_restore getty_restore device_exclusivity service_hardening service_results
 socket_cleanup archive_validation journal_evidence)
declare -A result; for key in "${results[@]}"; do result[$key]=not-run; done
rm -rf "$dumps" "$scenes" "$drm_dir" "$control"; mkdir -p "$artifact_dir" "$dumps" "$scenes" "$drm_dir" "$control"
rm -f "$artifact_dir"/milestone10-*
touch "$runtime_log" "$artifact_dir/milestone10-meson-test.log" \
  "$artifact_dir/milestone10-apps.log" \
  "$artifact_dir/milestone10-screenshot-validation.log"
capture_vt_state() {
  python3 - "$target_vt" "$1" <<'PY'
import fcntl,json,os,struct,sys
tty,output=sys.argv[1:]
VT_GETMODE=0x5601; VT_GETSTATE=0x5603; KDGETMODE=0x4B3B
fd=os.open(tty,os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
    state=bytearray(struct.calcsize('=HHH'))
    mode=bytearray(struct.calcsize('=BBhhh'))
    kd=bytearray(struct.calcsize('=i'))
    fcntl.ioctl(fd,VT_GETSTATE,state,True)
    fcntl.ioctl(fd,VT_GETMODE,mode,True)
    fcntl.ioctl(fd,KDGETMODE,kd,True)
finally:
    os.close(fd)
active,signal,state_mask=struct.unpack('=HHH',state)
vt_mode,waitv,relsig,acqsig,frsig=struct.unpack('=BBhhh',mode)
kd_mode,=struct.unpack('=i',kd)
payload={'tty':tty,'active_vt':active,'vt_signal':signal,'vt_state_mask':state_mask,
         'vt_mode':{'value':vt_mode,'name':{0:'auto',1:'process',2:'ack-acquire'}.get(vt_mode,'unknown'),
                    'waitv':waitv,'relsig':relsig,'acqsig':acqsig,'frsig':frsig},
         'kd_mode':{'value':kd_mode,'name':{0:'text',1:'graphics',2:'text0',3:'text1'}.get(kd_mode,'unknown')}}
with open(output,'w',encoding='utf-8') as stream:
    json.dump(payload,stream,sort_keys=True,indent=2); stream.write('\n')
PY
}
capture_getty_state() {
  getty_active_before=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
  getty_enabled_before=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
  [[ -n $getty_active_before && -n $getty_enabled_before ]]
  getty_was_active=false
  [[ $getty_active_before != active ]] || getty_was_active=true
  getty_state_captured=true
}
restore_getty_state() {
  [[ $getty_state_captured == true ]] || return 0
  if [[ $getty_active_before == active ]]; then
    systemctl start "$getty_unit" >/dev/null 2>&1 || return 1
  else
    systemctl stop "$getty_unit" >/dev/null 2>&1 || return 1
  fi
  getty_active_after=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
  getty_enabled_after=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
  printf 'getty_restore unit=%s active=%s enabled=%s\n' \
    "$getty_unit" "$getty_active_after" "$getty_enabled_after"
  [[ $getty_active_after == "$getty_active_before" &&
     $getty_enabled_after == "$getty_enabled_before" ]]
}
unit_property_equals() {
  local unit=$1 property=$2 expected=$3 actual
  actual=$(systemctl show "$unit" --property="$property" --value)
  [[ $actual == "$expected" ]] || {
    printf '%s property %s is %q, expected %q\n' "$unit" "$property" "$actual" "$expected" >&2
    return 1
  }
  printf 'unit_property unit=%s %s=%s\n' "$unit" "$property" "$actual"
}
unit_property_contains() {
  local unit=$1 property=$2 expected=$3 actual
  actual=$(systemctl show "$unit" --property="$property" --value)
  [[ $actual == *"$expected"* ]] || {
    printf '%s property %s does not contain %q: %q\n' "$unit" "$property" "$expected" "$actual" >&2
    return 1
  }
  printf 'unit_property unit=%s %s=%s\n' "$unit" "$property" "$actual"
}
verify_service_success() {
  local unit
  for unit in gwm-m10.service gwcomp-m10.service glasswyrmd-m10.service; do
    unit_property_equals "$unit" Result success
    unit_property_equals "$unit" ExecMainStatus 0
  done
}
record_facts() {
  status=$?; set +e; scenario_exit=$status
  if [[ -n $clients_pid ]]; then
    touch "$control/stop-clients"
    kill "$clients_pid" >/dev/null 2>&1 || true
    wait "$clients_pid" >/dev/null 2>&1 || true
  fi
  systemctl stop glasswyrmd-m10.service gwcomp-m10.service gwm-m10.service >/dev/null 2>&1
  if restore_getty_state; then
    [[ $getty_state_captured == false ]] || result[getty_restore]=passed
  else
    result[getty_restore]=failed
  fi
  journalctl -u glasswyrmd-m10.service --no-pager >"$artifact_dir/milestone10-glasswyrmd-journal.log" 2>&1
  journalctl -u gwm-m10.service --no-pager >"$artifact_dir/milestone10-gwm-journal.log" 2>&1
  journalctl -u gwcomp-m10.service --no-pager >"$artifact_dir/milestone10-gwcomp-journal.log" 2>&1
  [[ -s $artifact_dir/milestone10-glasswyrmd-journal.log && -s $artifact_dir/milestone10-gwm-journal.log && -s $artifact_dir/milestone10-gwcomp-journal.log ]] && result[journal_evidence]=passed
  [[ ! -e /run/glasswyrm-m10-gwm/gwm.sock && ! -e /run/glasswyrm-m10-gwcomp/gwcomp.sock && ! -e /run/glasswyrm-m10-input/input.sock && ! -e /tmp/.X11-unix/X99 ]] && result[socket_cleanup]=passed
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$scenario_exit"
    printf 'api_version=0.5.0\nsoversion=0\nwire_version=1.0\n'
    printf 'compiler_c=%s\ncompiler_cxx=%s\n' "$(cc --version | head -n1)" "$(c++ --version | head -n1)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' "$(meson --version)" "$(ninja --version)" "$(systemctl --version | head -n1)"
    printf 'libdrm_version=%s\n' "$(pkg-config --modversion libdrm 2>/dev/null || echo unavailable)"
    printf 'x_servers_absent=true\nmesa_absent=true\nlibinput_absent=true\n'
    printf 'drm_primary_node=%s\ndrm_driver=%s\ndrm_connector=%s\ndrm_mode=1024x768\n' "$drm_device" "${drm_driver:-unknown}" "$connector"
    printf 'drm_crtc=%s\ndrm_primary_plane=%s\ndumb_buffer=%s\natomic_capability=%s\ndrm_api=%s\natomic_test_only=%s\n' "${drm_crtc:-unknown}" "${drm_plane:-unknown}" "${dumb_buffer:-unknown}" "${atomic_capability:-unknown}" "${drm_api:-unknown}" "${atomic_test_only:-unknown}"
    printf 'canonical_hash=%s\nscanout_hash=%s\nmirror_hash=%s\nscreenshot_hash=%s\n' "${canonical_hash:-unknown}" "${scanout_hash:-unknown}" "${mirror_hash:-unknown}" "${screenshot_hash:-unknown}"
    printf 'getty_unit=%s\ngetty_active_before=%s\ngetty_enabled_before=%s\ngetty_active_after=%s\ngetty_enabled_after=%s\n' "$getty_unit" "$getty_active_before" "$getty_enabled_before" "$getty_active_after" "$getty_enabled_after"
    for key in "${results[@]}"; do printf '%s=%s\n' "$key" "${result[$key]}"; done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT
exec > >(tee -a "$runtime_log") 2>&1

# This guard intentionally precedes every emerge invocation. The harness never installs,
# rebuilds, selects, or loads a kernel; the clean snapshot must already provide DRM.
[[ -c $drm_device && -L /sys/class/drm/${drm_device##*/}/device/driver ]] || {
  echo 'M10 prerequisite failed before package installation: selected DRM primary node or kernel driver disappeared.' >&2; exit 20; }
qlist -IC x11-libs/libdrm && {
  echo 'M10 clean-snapshot headless gate requires libdrm to be absent.' >&2; exit 1; }
failure_stage=headless-without-libdrm
meson setup "$headless" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=false -Dheadless_backend=true
meson compile -C "$headless"
meson test -C "$headless" --print-errorlogs | tee "$artifact_dir/milestone10-meson-test.log"
! ldd "$headless/src/gwcomp" | grep -F libdrm
result[headless_no_libdrm]=passed

failure_stage=dependency-installation
emerge --oneshot --noreplace dev-build/meson dev-build/ninja virtual/pkgconfig net-misc/curl dev-build/make \
  x11-libs/libdrm x11-libs/libxcb x11-base/xcb-proto x11-libs/libX11 x11-libs/libXt \
  x11-libs/libXaw x11-libs/libXmu x11-libs/libXpm x11-libs/libXi x11-libs/libXft \
  x11-libs/libXext x11-libs/libXrender x11-libs/libxkbfile
for forbidden in x11-base/xorg-server x11-base/xwayland gui-libs/wayland media-libs/mesa dev-libs/libinput; do
  qlist -IC "$forbidden" && { printf 'M10 forbidden guest package is installed: %s\n' "$forbidden" >&2; exit 1; }
done
rm -rf "$client_dir"; mkdir -p "$client_dir"
manifest_value() {
  local application=$1 key=$2
  awk -v application="$application" -v key="$key" '
    /^\[\[client\]\]/ { matched=0 }
    $0 == "application = \"" application "\"" { matched=1 }
    matched && index($0, key " = \"")==1 {
      value=$0; sub(/^[^=]*= \"/, "", value); sub(/\"$/, "", value); print value; exit
    }' "$source_dir/tests/compat/m9/clients.toml"
}
client_version() {
  "$1" -version 2>&1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1
}
prepare_client() {
  local application=$1 binary=$2 expected=$3 url hash archive unpacked client_build prefix found
  if found=$(command -v "$binary" 2>/dev/null) &&
     [[ $(client_version "$found") == "$expected" ]]; then
    install -m755 "$found" "$client_dir/$binary"; return
  fi
  url=$(manifest_value "$application" source_url)
  hash=$(manifest_value "$application" source_sha256)
  [[ -n $url && $hash =~ ^[0-9a-f]{64}$ ]]
  archive="$client_dir/${url##*/}"
  curl --fail --location --proto '=https' --tlsv1.2 --output "$archive" "$url"
  printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --status
  unpacked="$client_dir/src-$binary"; mkdir -p "$unpacked"
  tar -xf "$archive" -C "$unpacked" --strip-components=1
  client_build="$client_dir/build-$binary"; prefix="$client_dir/prefix-$binary"
  if [[ -f $unpacked/meson.build ]]; then
    meson setup "$client_build" "$unpacked" --prefix="$prefix"
    meson compile -C "$client_build"; meson install -C "$client_build"
  else
    (cd "$unpacked" && ./configure --prefix="$prefix" && make -j2 && make install)
  fi
  install -m755 "$prefix/bin/$binary" "$client_dir/$binary"
  [[ $(client_version "$client_dir/$binary") == "$expected" ]]
}
prepare_client xeyes xeyes 1.3.1
prepare_client xclock-analog xclock 1.2.0

failure_stage=build-matrix
meson setup "$runtime" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true -Dm9_xeyes="$client_dir/xeyes" -Dm9_xclock="$client_dir/xclock"
meson compile -C "$runtime"; meson test -C "$runtime" --print-errorlogs | tee -a "$artifact_dir/milestone10-meson-test.log"
grep -E 'drm-ipc-integration.*OK' "$artifact_dir/milestone10-meson-test.log" >/dev/null
result[dual_backend]=passed result[strict_tests]=passed result[refactor_parity]=passed
result[delayed_ack]=passed result[delayed_release]=passed
meson setup "$drm_only" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=false -Drender_software=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false
meson compile -C "$drm_only"; meson test -C "$drm_only" --print-errorlogs; result[drm_only]=passed
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true -Dasan=true -Dubsan=true
meson compile -C "$asan"; meson test -C "$asan" --timeout-multiplier 3 --print-errorlogs; result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ meson setup "$build" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true
  meson compile -C "$build"; meson test -C "$build" --print-errorlogs; result[clang]=passed
else result[clang]=unavailable; fi
"$source_dir/tests/tools/source_layout_test.sh" "$source_dir"; result[source_layout_audit]=passed result[source_layout_budget]=passed
for spec in "$server|-Dwerror=true -Dlibgwipc=false -Dgwm=false -Dgwcomp=false -Dtools=false" "$server|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false" "$headless|-Dwerror=true -Ddrm_backend=false -Dheadless_backend=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false" "$gwm_build|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false" "$ipc_only|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false"; do
  dir=${spec%%|*}; opts=${spec#*|}; meson setup "$dir" "$source_dir" --wipe $opts; meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs
done
result[historical_components]=passed result[m4_m9_regressions]=passed

failure_stage=drm-probe
"$runtime/tools/gw_drm_probe" --device auto --require-mode 1024x768 --output "$artifact_dir/milestone10-drm-probe.json"
python3 - "$artifact_dir/milestone10-drm-probe.json" <<'PY' >"$drm_dir/probe.env"
import json,sys
d=json.load(open(sys.argv[1])); s=d['selected_candidate']; c=d['capabilities']
print(f"probed_device={d['device']['path']}")
print(f"probed_connector={s['connector']}")
print(f"drm_driver={d['driver']['name']}")
print(f"drm_crtc={s['crtc_id']}")
print(f"drm_plane={s['plane_id']}")
print(f"dumb_buffer={str(c['dumb_buffer']).lower()}")
print(f"atomic_capability={str(c['atomic']).lower()}")
PY
# shellcheck disable=SC1090
source "$drm_dir/probe.env"
[[ $probed_device =~ ^/dev/dri/card[0-9]+$ && -c $probed_device &&
   $probed_connector != */* && -n $probed_connector ]]
drm_device=$probed_device connector=$probed_connector
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --require-mode 1024x768 --snapshot-state --output "$artifact_dir/milestone10-kms-before.json"
capture_vt_state "$artifact_dir/milestone10-vt-before.json"
capture_getty_state
[[ $getty_was_active == false ]] || systemctl stop "$getty_unit"

failure_stage=drm-runtime
gwm_socket=/run/glasswyrm-m10-gwm/gwm.sock comp_socket=/run/glasswyrm-m10-gwcomp/gwcomp.sock input_socket=/run/glasswyrm-m10-input/input.sock
service_hardening=(--property=Type=exec --property=NoNewPrivileges=yes
 --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet=
 --property=AmbientCapabilities= --property=Restart=no)
systemd-run --unit=gwm-m10 "${service_hardening[@]}" --property=RuntimeDirectory=glasswyrm-m10-gwm --property=PrivateDevices=yes --no-block -- "$runtime/src/gwm" --ipc-socket "$gwm_socket"
systemd-run --unit=gwcomp-m10 "${service_hardening[@]}" --property=RuntimeDirectory=glasswyrm-m10-gwcomp --property=PrivateDevices=no --property=DevicePolicy=closed --property="DeviceAllow=$drm_device rw" --property="DeviceAllow=$target_vt rw" --property=StandardInput=tty-force --property="TTYPath=$target_vt" --property=TTYReset=yes --property=TTYVHangup=yes --property=TTYVTDisallocate=no --no-block -- "$runtime/src/gwcomp" --backend drm --ipc-socket "$comp_socket" --drm-device "$drm_device" --tty "$target_vt" --connector "$connector" --mode 1024x768 --drm-api auto --mirror-dump-dir "$dumps" --scene-manifest "$scenes/scene.jsonl" --drm-report "$artifact_dir/milestone10-drm-report.jsonl"
for _ in {1..200}; do [[ -S $comp_socket ]] && break; sleep .05; done; [[ -S $comp_socket ]]
systemd-run --unit=glasswyrmd-m10 "${service_hardening[@]}" --property=RuntimeDirectory=glasswyrm-m10-input --property=PrivateDevices=yes --property=PrivateTmp=no --no-block -- "$runtime/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket" --software-content --synthetic-input-socket "$input_socket"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 && -S $input_socket ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 && -S $input_socket ]]
for unit in gwm-m10.service glasswyrmd-m10.service; do
  unit_property_equals "$unit" Type exec
  unit_property_equals "$unit" NoNewPrivileges yes
  unit_property_equals "$unit" PrivateDevices yes
  unit_property_contains "$unit" RestrictAddressFamilies AF_UNIX
  unit_property_equals "$unit" CapabilityBoundingSet ''
  unit_property_equals "$unit" AmbientCapabilities ''
  unit_property_equals "$unit" Restart no
done
unit_property_equals gwcomp-m10.service Type exec
unit_property_equals gwcomp-m10.service NoNewPrivileges yes
unit_property_equals gwcomp-m10.service PrivateDevices no
unit_property_equals gwcomp-m10.service DevicePolicy closed
unit_property_contains gwcomp-m10.service DeviceAllow "$drm_device rw"
unit_property_contains gwcomp-m10.service DeviceAllow "$target_vt rw"
unit_property_contains gwcomp-m10.service RestrictAddressFamilies AF_UNIX
unit_property_equals gwcomp-m10.service CapabilityBoundingSet ''
unit_property_equals gwcomp-m10.service AmbientCapabilities ''
unit_property_equals gwcomp-m10.service Restart no
unit_property_equals gwcomp-m10.service StandardInput tty-force
unit_property_equals gwcomp-m10.service TTYPath "$target_vt"
unit_property_equals gwcomp-m10.service TTYReset yes
unit_property_equals gwcomp-m10.service TTYVHangup yes
unit_property_equals gwcomp-m10.service TTYVTDisallocate no
result[service_hardening]=passed
"$source_dir/tests/apps/m10_live_combined.sh" \
  "$client_dir/xeyes" "$client_dir/xclock" \
  "$runtime/tests/libgw_m9_fixed_time.so" "$runtime/tests/gwinput_m8" \
  "$input_socket" "$control" >"$artifact_dir/milestone10-apps.log" 2>&1 &
clients_pid=$!
for _ in {1..600}; do [[ -f $control/clients-ready ]] && break; sleep .05; done
[[ -f $control/clients-ready ]] && kill -0 "$clients_pid"
m9_golden=$source_dir/tests/fixtures/m9/combined.ppm
[[ -s $m9_golden ]] || { printf 'M9 combined golden is unavailable: %s\n' "$m9_golden" >&2; exit 1; }
mirror=
for _ in {1..600}; do
  mirror=$(find "$dumps" -type f -name '*.ppm' -print | sort | tail -n1)
  [[ -n $mirror && -s $mirror ]] && cmp -s "$mirror" "$m9_golden" && break
  sleep .05
done
[[ -n $mirror && -s $mirror ]] && cmp "$mirror" "$m9_golden" || {
  sha256sum "$mirror" "$m9_golden" >&2
  printf 'M10 canonical mirror differs from the accepted M9 combined golden.\n' >&2
  exit 1
}
result[m9_golden_mirror]=passed
python3 - "$artifact_dir/milestone10-drm-report.jsonl" <<'PY' >"$drm_dir/report.env"
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
selection=next(r for r in records if r.get('record')=='selection')
next(r for r in records if r.get('record')=='modeset')
frame=next(r for r in reversed(records) if r.get('record')=='flip')
api=selection['api']
print(f"drm_api={api}")
print(f"canonical_hash={frame['canonical_hash']}")
print(f"scanout_hash={frame['scanout_hash']}")
PY
# shellcheck disable=SC1090
source "$drm_dir/report.env"
[[ -n $canonical_hash && $canonical_hash == "$scanout_hash" ]]; result[hash_parity]=passed result[initial_modeset]=passed result[page_flip]=passed
if [[ $drm_api == atomic ]]; then
  atomic_test_only=passed
elif [[ $atomic_capability == true ]]; then
  atomic_test_only=failed-fallback
  journalctl -u gwcomp-m10.service --no-pager |
    grep -F 'atomic DRM fallback:' >/dev/null
else
  atomic_test_only=unsupported
fi
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --require-mode 1024x768 --expect-active --output "$artifact_dir/milestone10-kms-active.json"
gwcomp_pid=$(systemctl show gwcomp-m10.service --property=MainPID --value)
drm_clients=/sys/kernel/debug/dri/${drm_device##*card}/clients
[[ $gwcomp_pid =~ ^[1-9][0-9]*$ && -r $drm_clients ]] || {
  printf 'Cannot prove DRM master exclusivity from %s for PID %s.\n' "$drm_clients" "$gwcomp_pid" >&2
  exit 1
}
python3 - "$drm_clients" "$gwcomp_pid" <<'PY'
import pathlib,sys
lines=[line.split() for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if line.strip()]
if len(lines)<2: raise SystemExit('DRM clients evidence is empty')
header=[value.lower() for value in lines[0]]
try:
    pid_index=header.index('pid') if 'pid' in header else header.index('tgid')
    master_index=header.index('master')
except ValueError as error:
    raise SystemExit('DRM clients evidence lacks pid-or-tgid/master columns') from error
masters=[row for row in lines[1:] if len(row)>max(pid_index,master_index) and row[master_index].lower() in ('y','yes','1')]
if len(masters)!=1 or masters[0][pid_index]!=sys.argv[2]:
    raise SystemExit('gwcomp is not the sole DRM master')
other_clients=[row for row in lines[1:] if len(row)>pid_index and row[pid_index]!=sys.argv[2]]
if other_clients: raise SystemExit('another process has the DRM primary node open')
PY
python3 - "$drm_device" "$target_vt" "$gwcomp_pid" <<'PY'
import glob,os,stat,sys
drm,tty,pid=sys.argv[1:]; expected=int(pid)
targets={}
for path in [drm,tty,*sorted(glob.glob('/dev/input/event*'))]:
    try:
        status=os.stat(path)
        if stat.S_ISCHR(status.st_mode): targets[status.st_rdev]=path
    except FileNotFoundError: pass
owners={path:set() for path in targets.values()}
for process in glob.glob('/proc/[0-9]*'):
    process_id=int(process.rsplit('/',1)[1])
    for descriptor in glob.glob(process+'/fd/*'):
        try: status=os.stat(descriptor)
        except OSError: continue
        path=targets.get(status.st_rdev) if stat.S_ISCHR(status.st_mode) else None
        if path: owners[path].add(process_id)
if owners.get(drm,set())!={expected}: raise SystemExit(f'unexpected DRM owners: {owners.get(drm,set())}')
if owners.get(tty,set())!={expected}: raise SystemExit(f'unexpected target VT owners: {owners.get(tty,set())}')
for path,pids in owners.items():
    if path.startswith('/dev/input/') and pids:
        raise SystemExit(f'real input device {path} is open by {sorted(pids)}')
PY
result[device_exclusivity]=passed
capture_vt_state "$artifact_dir/milestone10-vt-active.json"
python3 - "$artifact_dir/milestone10-vt-active.json" "${target_vt#/dev/tty}" <<'PY'
import json,sys
state=json.load(open(sys.argv[1])); expected=int(sys.argv[2])
if state['active_vt'] != expected: raise SystemExit('target VT is not active')
if state['kd_mode']['value'] != 1: raise SystemExit('target VT did not enter KD_GRAPHICS')
if state['vt_mode']['value'] != 1: raise SystemExit('target VT did not enter VT_PROCESS')
PY
printf 'ready\ncommit_id=combined\ngeneration=1\ncanonical_hash=%s\nscanout_hash=%s\nmode=1024x768\nconnector=%s\n' "$canonical_hash" "$scanout_hash" "$connector" >"$control/screenshot-ready"
for _ in {1..600}; do [[ -f $control/screen-captured ]] && break; sleep .1; done; [[ -f $control/screen-captured ]]
python3 - "$mirror" "$artifact_dir/milestone10-screen.ppm" <<'PY'
import pathlib,sys
def ppm(path):
    data=pathlib.Path(path).read_bytes(); position=0
    def token():
        nonlocal position
        while position < len(data):
            if data[position] == 35:
                position=data.find(b'\n',position)
                if position < 0: raise ValueError('unterminated PPM comment')
            elif chr(data[position]).isspace(): position += 1
            else: break
        start=position
        while position < len(data) and not chr(data[position]).isspace(): position += 1
        return data[start:position]
    if token() != b'P6': raise ValueError('screenshot is not P6 PPM')
    width,height,maximum=map(int,(token(),token(),token()))
    if position >= len(data) or not chr(data[position]).isspace(): raise ValueError('missing PPM payload separator')
    position += 1; pixels=data[position:]
    if maximum != 255 or len(pixels) != width*height*3: raise ValueError('invalid PPM payload')
    return width,height,pixels
left=ppm(sys.argv[1]); right=ppm(sys.argv[2])
if left[:2] != (1024,768) or right[:2] != left[:2] or right[2] != left[2]:
    raise SystemExit('graphical-console RGB payload differs from canonical mirror')
PY
mirror_hash=$(sha256sum "$mirror" | cut -d' ' -f1); screenshot_hash=$(sha256sum "$artifact_dir/milestone10-screen.ppm" | cut -d' ' -f1)
printf 'format=P6\nmode=1024x768\nmirror_sha256=%s\nscreenshot_sha256=%s\nexact_equal=true\n' "$mirror_hash" "$screenshot_hash" >>"$artifact_dir/milestone10-screenshot-validation.log"; result[screenshot_equal]=passed
chvt 1; chvt 2
python3 - "$artifact_dir/milestone10-drm-report.jsonl" "$canonical_hash" <<'PY'
import json,pathlib,sys,time
path=pathlib.Path(sys.argv[1]); expected=sys.argv[2]
deadline=time.monotonic()+5
while True:
    try:
        records=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        release=next(r for r in reversed(records) if r.get('record')=='vt' and r.get('transition')=='release')
        acquire=next(r for r in reversed(records) if r.get('record')=='vt' and r.get('transition')=='acquire')
        assert release['master_owned'] is False and release['full_modeset'] is False
        assert acquire['master_owned'] is True and acquire['full_modeset'] is True
        assert acquire['committed_hash'] == expected
        break
    except (AssertionError,KeyError,StopIteration,json.JSONDecodeError):
        if time.monotonic() >= deadline: raise SystemExit('VT release/acquire report proof is incomplete')
        time.sleep(.05)
PY
result[vt_release]=passed result[vt_acquire]=passed result[remodeset]=passed
pre_repaint_ordinal=$(python3 - "$artifact_dir/milestone10-drm-report.jsonl" <<'PY'
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
print(max(r['ordinal'] for r in records if r.get('record') in ('modeset','flip')))
PY
)
touch "$control/post-vt-input"
for _ in {1..600}; do [[ -f $control/post-vt-input-complete ]] && break; sleep .05; done
[[ -f $control/post-vt-input-complete ]] && kill -0 "$clients_pid"
python3 - "$artifact_dir/milestone10-drm-report.jsonl" "$pre_repaint_ordinal" "$canonical_hash" <<'PY'
import json,pathlib,sys,time
path=pathlib.Path(sys.argv[1]); before=int(sys.argv[2]); expected=sys.argv[3]
deadline=time.monotonic()+5
while True:
    try:
        records=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        flips=[r for r in records if r.get('record')=='flip' and r['ordinal']>before]
        if flips and flips[-1]['canonical_hash']==expected: break
    except json.JSONDecodeError: pass
    if time.monotonic() >= deadline:
        raise SystemExit('post-VT xeyes repaint did not complete a later matching page flip')
    time.sleep(.05)
PY
mirror=$(find "$dumps" -type f -name '*.ppm' -print | sort | tail -n1)
cmp "$mirror" "$m9_golden"
result[post_vt_repaint]=passed
printf 'ready\ncommit_id=post-vt\ngeneration=1\ncanonical_hash=%s\nscanout_hash=%s\nmode=1024x768\nconnector=%s\n' "$canonical_hash" "$scanout_hash" "$connector" >"$control/screenshot-after-vt-ready"
for _ in {1..600}; do [[ -f $control/screen-after-vt-captured ]] && break; sleep .1; done; [[ -f $control/screen-after-vt-captured ]]
python3 - "$mirror" "$artifact_dir/milestone10-screen-after-vt.ppm" <<'PY'
import pathlib,sys
def payload(path):
    data=pathlib.Path(path).read_bytes(); parts=data.split(maxsplit=4)
    if len(parts)!=5 or parts[0]!=b'P6' or parts[1:4]!=[b'1024',b'768',b'255'] or len(parts[4])!=1024*768*3:
        raise SystemExit('invalid 1024x768 P6 screenshot')
    return parts[4]
if payload(sys.argv[1]) != payload(sys.argv[2]): raise SystemExit('post-VT RGB payload differs from mirror')
PY
result[post_vt_screenshot_equal]=passed

touch "$control/stop-clients"
wait "$clients_pid"
clients_pid=
systemctl stop glasswyrmd-m10.service; systemctl stop gwcomp-m10.service
python3 - "$artifact_dir/milestone10-drm-report.jsonl" <<'PY'
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
restore=next(r for r in reversed(records) if r.get('record')=='restore')
if not all(restore.get(key) is True for key in ('kms','vt','master_drop','framebuffer_cleanup')):
    raise SystemExit('DRM shutdown report does not prove complete restoration')
PY
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --require-mode 1024x768 --expect-restored "$artifact_dir/milestone10-kms-before.json" --output "$artifact_dir/milestone10-kms-after.json"
capture_vt_state "$artifact_dir/milestone10-vt-after.json"
python3 - "$artifact_dir/milestone10-vt-before.json" "$artifact_dir/milestone10-vt-after.json" <<'PY'
import json,sys
before=json.load(open(sys.argv[1])); after=json.load(open(sys.argv[2]))
checks={'active VT':('active_vt',),'KD mode':('kd_mode','value'),'VT mode':('vt_mode','value'),
        'VT wait policy':('vt_mode','waitv'),'VT release signal':('vt_mode','relsig'),
        'VT acquire signal':('vt_mode','acqsig'),'VT forced-release signal':('vt_mode','frsig')}
for label,path in checks.items():
    left,right=before,after
    for key in path: left=left[key]; right=right[key]
    if left != right: raise SystemExit(f'{label} was not restored: {left!r} != {right!r}')
PY
result[kms_restore]=passed result[kd_restore]=passed result[vt_mode_restore]=passed result[active_vt_restore]=passed
systemctl stop gwm-m10.service
verify_service_success; result[service_results]=passed
restore_getty_state; result[getty_restore]=passed
evidence=$drm_dir/evidence; mkdir -p "$evidence"; cp "$mirror" "$evidence/canonical.ppm"; cp "$artifact_dir/milestone10-screen.ppm" "$artifact_dir/milestone10-screen-after-vt.ppm" "$evidence/"
cp "$scenes/scene.jsonl" "$evidence/"; find "$dumps" -name frames.jsonl -exec cp {} "$evidence/frames.jsonl" \;
cp "$artifact_dir"/milestone10-{drm-report.jsonl,kms-before.json,kms-active.json,kms-after.json,vt-before.json,vt-active.json,vt-after.json} "$evidence/"
(cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone10-drm-evidence.tar" ./*)
result[archive_validation]=passed failure_stage= scenario_exit=0
GUEST_SCRIPT
}

milestone10_poll_marker() {
  local marker=$1 guest_pid=$2 output='' canonical scanout
  local deadline=$((SECONDS + M10_SCREENSHOT_WAIT_SECONDS))
  while ((SECONDS < deadline)); do
    if output=$(guest_run_script 'set -euo pipefail; test -f "$1"; cat "$1"' "$M10_GUEST_CONTROL_DIR/$marker" 2>/dev/null); then
      canonical=$(sed -n 's/^canonical_hash=//p' <<<"$output")
      scanout=$(sed -n 's/^scanout_hash=//p' <<<"$output")
      if grep -Fxq ready <<<"$output" && grep -Fxq mode=1024x768 <<<"$output" &&
        grep -q '^commit_id=.' <<<"$output" && grep -q '^generation=.' <<<"$output" &&
        grep -q '^connector=.' <<<"$output" && [[ -n $canonical && $canonical == "$scanout" ]]; then
        printf '%s\n' "$output"; return 0
      fi
    fi
    kill -0 "$guest_pid" 2>/dev/null || return 1
    sleep .1
  done
  printf 'Timed out waiting for fixed M10 marker %s.\n' "$marker" >&2; return 1
}

milestone10_capture_screen() {
  local ready=$1 captured=$2 name=$3 guest_pid=$4 marker
  marker=$(milestone10_poll_marker "$ready" "$guest_pid") || return
  printf '%s\n' "$marker" >>"$ARTIFACTS_PATH_ABS/milestone10-screenshot-validation.log"
  virsh --connect "$LIBVIRT_URI" screenshot "$VM_DOMAIN" "$ARTIFACTS_PATH_ABS/$name" || return
  [[ -s $ARTIFACTS_PATH_ABS/$name ]] || { printf 'virsh screenshot did not produce %s\n' "$name" >&2; return 1; }
  ssh_arguments
  rsync -a -e "ssh -p $SSH_PORT -o BatchMode=yes -o ConnectTimeout=10" "$ARTIFACTS_PATH_ABS/$name" "$SSH_TARGET:$M10_GUEST_ARTIFACT_DIR/$name" || return
  guest_run_script 'set -euo pipefail; marker=$1; mkdir -p "${marker%/*}"; printf "screen-captured\n" >"$marker"' "$M10_GUEST_CONTROL_DIR/$captured"
}

milestone10_release_guest_waits() {
  guest_run_script 'set -euo pipefail; for marker in "$@"; do mkdir -p "${marker%/*}"; printf "host-capture-failed\n" >"$marker"; done' \
    "$M10_GUEST_CONTROL_DIR/screen-captured" "$M10_GUEST_CONTROL_DIR/screen-after-vt-captured" >/dev/null 2>&1 || true
}

collect_milestone10_artifacts() {
  local name failed=0
  init_artifacts
  for name in "${M10_TEXT_ARTIFACTS[@]}"; do
    [[ $name == milestone10-runtime-test.log || $name == milestone10-screenshot-validation.log ]] && continue
    guest_run_script 'set -euo pipefail; cat "$1"' "$M10_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  ssh_arguments
  for name in "${M10_BINARY_ARTIFACTS[@]}"; do
    [[ $name == milestone10-screen.ppm || $name == milestone10-screen-after-vt.ppm ]] && continue
    scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M10_GUEST_ARTIFACT_DIR/$name" "$ARTIFACTS_PATH_ABS/$name" || failed=1
  done
  return "$failed"
}

validate_milestone10_archive() {
  local archive=$ARTIFACTS_PATH_ABS/milestone10-drm-evidence.tar listing member scratch status=0
  [[ -s $archive && -s $ARTIFACTS_PATH_ABS/milestone10-screen.ppm && -s $ARTIFACTS_PATH_ABS/milestone10-screen-after-vt.ppm ]] || return
  listing=$(tar -tf "$archive") || return
  for member in canonical.ppm milestone10-screen.ppm milestone10-screen-after-vt.ppm frames.jsonl scene.jsonl milestone10-drm-report.jsonl milestone10-kms-before.json milestone10-kms-active.json milestone10-kms-after.json milestone10-vt-before.json milestone10-vt-active.json milestone10-vt-after.json SHA256SUMS; do
    grep -Eq "^(\./)?$member$" <<<"$listing" || return
  done
  scratch=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m10-evidence.XXXXXX") || return
  tar -xf "$archive" -C "$scratch" || status=$?
  if ((status == 0)); then (cd "$scratch" && sha256sum --check --status SHA256SUMS) || status=$?; fi
  rm -rf -- "$scratch"
  return "$status"
}

write_milestone10_summary() {
  local requested=$1 failure=${2:-} facts=$ARTIFACTS_PATH_ABS/milestone10-facts.env out=$ARTIFACTS_PATH_ABS/milestone10-summary.json
  python3 - "$facts" "$out" "$requested" "$failure" "$M10_REQUIRED_BASE_COMMIT" "${M10_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    key,sep,value=line.partition('=')
    if sep: facts[key]=value
required='strict_tests source_layout_audit source_layout_budget refactor_parity sanitizer headless_no_libdrm dual_backend drm_only historical_components m4_m9_regressions m9_golden_mirror initial_modeset page_flip delayed_ack delayed_release hash_parity screenshot_equal vt_release vt_acquire remodeset post_vt_repaint post_vt_screenshot_equal kms_restore kd_restore vt_mode_restore active_vt_restore getty_restore device_exclusivity service_hardening service_results socket_cleanup archive_validation journal_evidence'.split()
identity={'api_version':'0.5.0','soversion':'0','wire_version':'1.0','drm_mode':'1024x768','x_servers_absent':'true','mesa_absent':'true','libinput_absent':'true'}
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
errors += [f'{k} must be {v}' for k,v in identity.items() if facts.get(k)!=v]
for k in ('drm_primary_node','drm_driver','drm_connector','drm_crtc','drm_primary_plane','dumb_buffer','atomic_capability','drm_api','canonical_hash','scanout_hash','mirror_hash','screenshot_hash','compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','libdrm_version'):
  if facts.get(k) in (None,'','unknown','unavailable'): errors.append(f'{k} must be recorded')
if facts.get('canonical_hash') != facts.get('scanout_hash'): errors.append('canonical and scanout hashes differ')
for state in ('active','enabled'):
  before=facts.get(f'getty_{state}_before'); after=facts.get(f'getty_{state}_after')
  if before in (None,'','unknown') or after != before:
    errors.append(f'getty {state} state was not captured and restored')
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
payload={'required_base_commit':base,'tested_commit':tested,'facts':facts,'results':{k:facts.get(k,'unknown') for k in required},'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone10_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 preflight='' script='' guest_pid=0 guest_status=0
  require_approval milestone10-runtime-test "$approved"; require_vm_domain
  is_true "$SNAPSHOT_ENABLED" || die 'milestone10-runtime-test requires the configured M9-clean snapshot.'
  note 'Required gate sequence: reset; milestone9-runtime-test; reset; milestone10-runtime-test. M9 must run before M10 installs libdrm.'
  init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone10_evidence || { status=$?; failure=source-evidence; }
  [[ -n $failure ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z $failure ]]; then
    if milestone10_doctor | tee "$ARTIFACTS_PATH_ABS/milestone10-runtime-test.log"; then :; else status=$?; failure=host-guest-doctor; fi
  fi
  if [[ -z $failure ]]; then
    if preflight=$(guest_run_script "$(milestone10_guest_prerequisite_script)" 2>&1); then
      printf '%s\n' "$preflight" | tee -a "$ARTIFACTS_PATH_ABS/milestone10-runtime-test.log"
    else status=$?; printf '%s\n' "$preflight" | tee -a "$ARTIFACTS_PATH_ABS/milestone10-runtime-test.log" >&2; failure=graphical-prerequisite; fi
  fi
  if [[ -z $failure ]]; then
    if verify_milestone10_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z $failure ]]; then
    drm_device=$(sed -n 's/^drm_primary_node=//p' <<<"$preflight"); connector=$(sed -n 's/^drm_connector=//p' <<<"$preflight"); target_vt=$(sed -n 's/^target_vt=//p' <<<"$preflight")
    script=$(milestone10_guest_script)
    guest_run_script "$script" "$GUEST_SOURCE_PATH" "$M10_GUEST_ARTIFACT_DIR" "$drm_device" "$connector" "$target_vt" >>"$ARTIFACTS_PATH_ABS/milestone10-runtime-test.log" 2>&1 & guest_pid=$!
    if milestone10_capture_screen screenshot-ready screen-captured milestone10-screen.ppm "$guest_pid"; then :; else status=$?; failure=first-screenshot; milestone10_release_guest_waits; fi
    if [[ -z $failure ]]; then
      if milestone10_capture_screen screenshot-after-vt-ready screen-after-vt-captured milestone10-screen-after-vt.ppm "$guest_pid"; then :; else status=$?; failure=post-vt-screenshot; milestone10_release_guest_waits; fi
    fi
    if wait "$guest_pid"; then :; else guest_status=$?; if [[ -z $failure ]]; then status=$guest_status; failure=guest-runtime; fi; fi
  fi
  collect_milestone10_artifacts || collection=$?
  if ((collection)) && [[ -z $failure ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -z $failure ]] && ! validate_milestone10_archive; then status=1; failure=artifact-validation; fi
  if [[ -n $failure ]]; then write_milestone10_summary false "$failure" || true; printf 'Milestone 10 VM runtime test failed during: %s\n' "$failure" >&2; print_artifacts >&2; return "${status:-1}"; fi
  verify_milestone10_source_identity || { write_milestone10_summary false source-identity-changed || true; return 1; }
  write_milestone10_summary true ''; record_scenario milestone10-runtime-test passed "$ARTIFACTS_PATH_ABS/milestone10-runtime-test.log"
  printf 'Milestone 10 VM runtime test passed.\n'; print_artifacts
}
