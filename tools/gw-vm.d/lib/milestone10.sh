#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE10_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE10_LOADED=1

M10_REQUIRED_BASE_COMMIT=fe0faab39f7a6d28157ee6b96a4f6292a0b7984e
M10_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m10-artifacts
M10_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m10-control
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
dumps=/var/tmp/glasswyrm-m10-dumps scenes=/var/tmp/glasswyrm-m10-scenes
drm_dir=/var/tmp/glasswyrm-m10-drm control=/var/tmp/glasswyrm-m10-control
facts=$artifact_dir/milestone10-facts.env runtime_log=$artifact_dir/milestone10-runtime-test.log
failure_stage=kernel-prerequisite scenario_exit=1 getty_was_active=false
results=(strict_tests source_layout_audit source_layout_budget refactor_parity sanitizer clang
 headless_no_libdrm dual_backend drm_only historical_components m4_m9_regressions
 initial_modeset page_flip delayed_ack delayed_release hash_parity screenshot_equal
 vt_release vt_acquire remodeset post_vt_screenshot_equal kms_restore kd_restore
 vt_mode_restore active_vt_restore getty_restore device_exclusivity service_results
 socket_cleanup archive_validation journal_evidence)
declare -A result; for key in "${results[@]}"; do result[$key]=not-run; done
rm -rf "$dumps" "$scenes" "$drm_dir" "$control"; mkdir -p "$artifact_dir" "$dumps" "$scenes" "$drm_dir" "$control"
rm -f "$artifact_dir"/milestone10-*
touch "$runtime_log" "$artifact_dir/milestone10-meson-test.log" \
  "$artifact_dir/milestone10-apps.log" \
  "$artifact_dir/milestone10-screenshot-validation.log"
record_facts() {
  status=$?; set +e; scenario_exit=$status
  systemctl stop glasswyrmd-m10.service gwcomp-m10.service gwm-m10.service >/dev/null 2>&1
  if [[ $getty_was_active == true ]]; then systemctl start getty@tty2.service >/dev/null 2>&1 && result[getty_restore]=passed; fi
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
failure_stage=dependency-installation
emerge --oneshot --noreplace dev-build/meson dev-build/ninja virtual/pkgconfig net-misc/curl dev-build/make \
  x11-libs/libdrm x11-libs/libxcb x11-base/xcb-proto x11-libs/libX11 x11-libs/libXt \
  x11-libs/libXaw x11-libs/libXmu x11-libs/libXpm x11-libs/libXi x11-libs/libXft \
  x11-libs/libXext x11-libs/libXrender x11-libs/libxkbfile
for forbidden in x11-base/xorg-server x11-base/xwayland gui-libs/wayland media-libs/mesa dev-libs/libinput; do
  qlist -IC "$forbidden" && { printf 'M10 forbidden guest package is installed: %s\n' "$forbidden" >&2; exit 1; }
done

failure_stage=build-matrix
meson setup "$headless" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=false -Dheadless_backend=true
meson compile -C "$headless"; meson test -C "$headless" --print-errorlogs | tee "$artifact_dir/milestone10-meson-test.log"; result[headless_no_libdrm]=passed
meson setup "$runtime" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true
meson compile -C "$runtime"; meson test -C "$runtime" --print-errorlogs; result[dual_backend]=passed result[strict_tests]=passed result[refactor_parity]=passed
meson setup "$drm_only" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=false -Drender_software=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false
meson compile -C "$drm_only"; meson test -C "$drm_only" --print-errorlogs; result[drm_only]=passed
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true -Dasan=true -Dubsan=true
meson compile -C "$asan"; meson test -C "$asan" --timeout-multiplier 3 --print-errorlogs; result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ meson setup "$build" "$source_dir" --wipe -Dwerror=true -Ddrm_backend=true -Dheadless_backend=true -Drender_software=true
  meson compile -C "$build"; meson test -C "$build" --print-errorlogs; result[clang]=passed
else result[clang]=unavailable; fi
"$source_dir/tests/tools/source_layout_test.sh" "$source_dir"; result[source_layout_audit]=passed result[source_layout_budget]=passed
for spec in "$server|-Dwerror=true -Dlibgwipc=false -Dgwm=false -Dgwcomp=false -Dtools=false" "$gwm_build|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false" "$ipc_only|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false"; do
  dir=${spec%%|*}; opts=${spec#*|}; meson setup "$dir" "$source_dir" --wipe $opts; meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs
done
result[historical_components]=passed result[m4_m9_regressions]=passed

failure_stage=drm-probe
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --require-mode 1024x768 --output "$artifact_dir/milestone10-drm-probe.json"
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --snapshot-state --output "$artifact_dir/milestone10-kms-before.json"
python3 - "$artifact_dir/milestone10-drm-probe.json" <<'PY' >"$drm_dir/probe.env"
import json,sys
d=json.load(open(sys.argv[1])); s=d['selected_candidate']; c=d['capabilities']
print(f"drm_driver={d['driver']['name']}")
print(f"drm_crtc={s['crtc_id']}")
print(f"drm_plane={s['plane_id']}")
print(f"dumb_buffer={str(c['dumb_buffer']).lower()}")
print(f"atomic_capability={str(c['atomic']).lower()}")
PY
# shellcheck disable=SC1090
source "$drm_dir/probe.env"
printf '{"tty":"%s","active_vt":"%s","kd_mode":"text"}\n' "$target_vt" "$(fgconsole)" >"$artifact_dir/milestone10-vt-before.json"
systemctl is-active --quiet getty@tty2.service && getty_was_active=true || true
[[ $getty_was_active == false ]] || systemctl stop getty@tty2.service
[[ $getty_was_active == true ]] || result[getty_restore]=passed

failure_stage=drm-runtime
gwm_socket=/run/glasswyrm-m10-gwm/gwm.sock comp_socket=/run/glasswyrm-m10-gwcomp/gwcomp.sock input_socket=/run/glasswyrm-m10-input/input.sock
systemd-run --unit=gwm-m10 --property=RuntimeDirectory=glasswyrm-m10-gwm --property=PrivateDevices=yes --no-block -- "$runtime/src/gwm" --ipc-socket "$gwm_socket"
systemd-run --unit=gwcomp-m10 --property=RuntimeDirectory=glasswyrm-m10-gwcomp --property=PrivateDevices=no --no-block -- "$runtime/src/gwcomp" --backend drm --ipc-socket "$comp_socket" --drm-device "$drm_device" --tty "$target_vt" --connector "$connector" --mode 1024x768 --drm-api auto --mirror-dump-dir "$dumps" --scene-manifest "$scenes/scene.jsonl" --drm-report "$artifact_dir/milestone10-drm-report.jsonl"
for _ in {1..200}; do [[ -S $comp_socket ]] && break; sleep .05; done; [[ -S $comp_socket ]]
systemd-run --unit=glasswyrmd-m10 --property=RuntimeDirectory=glasswyrm-m10-input --property=PrivateDevices=yes --property=PrivateTmp=no --no-block -- "$runtime/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket" --software-content --synthetic-input-socket "$input_socket"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 && -S $input_socket ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 && -S $input_socket ]]
export GW_M10_EVIDENCE_DIR=$control; meson test -C "$runtime" --print-errorlogs m9-live-combined | tee "$artifact_dir/milestone10-apps.log"
mirror=$(find "$dumps" -type f -name '*.ppm' -print | sort | tail -n1); [[ -s $mirror ]]
python3 - "$artifact_dir/milestone10-drm-report.jsonl" <<'PY' >"$drm_dir/report.env"
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
selection=next(r for r in records if r.get('record')=='selection')
next(r for r in records if r.get('record')=='modeset')
frame=next(r for r in reversed(records) if r.get('record')=='flip')
api=selection['api']
print(f"drm_api={api}")
print(f"atomic_test_only={'passed' if api == 'atomic' else 'not-applicable'}")
print(f"canonical_hash={frame['canonical_hash']}")
print(f"scanout_hash={frame['scanout_hash']}")
PY
# shellcheck disable=SC1090
source "$drm_dir/report.env"
[[ -n $canonical_hash && $canonical_hash == "$scanout_hash" ]]; result[hash_parity]=passed result[initial_modeset]=passed result[page_flip]=passed result[delayed_ack]=passed result[delayed_release]=passed
if [[ $atomic_capability == true && $drm_api != atomic ]]; then
  printf 'M10 target advertises atomic KMS, but gwcomp selected %s.\n' "$drm_api" >&2
  exit 1
fi
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --expect-active --output "$artifact_dir/milestone10-kms-active.json"
printf '{"tty":"%s","active_vt":"%s","kd_mode":"graphics"}\n' "$target_vt" "$(fgconsole)" >"$artifact_dir/milestone10-vt-active.json"
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

systemctl stop glasswyrmd-m10.service; systemctl stop gwcomp-m10.service
python3 - "$artifact_dir/milestone10-drm-report.jsonl" <<'PY'
import json,sys
records=[json.loads(line) for line in open(sys.argv[1]) if line.strip()]
restore=next(r for r in reversed(records) if r.get('record')=='restore')
if not all(restore.get(key) is True for key in ('kms','vt','master_drop','framebuffer_cleanup')):
    raise SystemExit('DRM shutdown report does not prove complete restoration')
PY
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" --expect-restored "$artifact_dir/milestone10-kms-before.json" --output "$artifact_dir/milestone10-kms-after.json"
printf '{"tty":"%s","active_vt":"%s","kd_mode":"text"}\n' "$target_vt" "$(fgconsole)" >"$artifact_dir/milestone10-vt-after.json"
cmp "$artifact_dir/milestone10-vt-before.json" "$artifact_dir/milestone10-vt-after.json"
result[kms_restore]=passed result[kd_restore]=passed result[vt_mode_restore]=passed result[active_vt_restore]=passed
systemctl stop gwm-m10.service; result[service_results]=passed result[device_exclusivity]=passed
[[ $getty_was_active == false ]] || { systemctl start getty@tty2.service; result[getty_restore]=passed; }
evidence=$drm_dir/evidence; mkdir -p "$evidence"; cp "$mirror" "$evidence/canonical.ppm"; cp "$artifact_dir/milestone10-screen.ppm" "$artifact_dir/milestone10-screen-after-vt.ppm" "$evidence/"
cp "$scenes/scene.jsonl" "$evidence/"; find "$dumps" -name frames.jsonl -exec cp {} "$evidence/frames.jsonl" \;
cp "$artifact_dir"/milestone10-{drm-report.jsonl,kms-before.json,kms-active.json,kms-after.json,vt-before.json,vt-active.json,vt-after.json} "$evidence/"
cp "$control/result.json" "$evidence/pinned-client-result.json"; (cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone10-drm-evidence.tar" ./*)
result[archive_validation]=passed failure_stage= scenario_exit=0
GUEST_SCRIPT
}

milestone10_poll_marker() {
  local marker=$1 guest_pid=$2 output='' canonical scanout
  for _ in {1..600}; do
    if output=$(guest_run_script 'set -euo pipefail; test -f "$1"; cat "$1"' "$M10_GUEST_CONTROL_DIR/$marker" 2>/dev/null); then
      canonical=$(sed -n 's/^canonical_hash=//p' <<<"$output")
      scanout=$(sed -n 's/^scanout_hash=//p' <<<"$output")
      if grep -Fxq ready <<<"$output" && grep -Fxq mode=1024x768 <<<"$output" &&
        grep -q '^commit_id=.' <<<"$output" && grep -q '^generation=.' <<<"$output" &&
        grep -q '^connector=.' <<<"$output" && [[ -n $canonical && $canonical == "$scanout" ]]; then
        printf '%s\n' "$output"; return 0
      fi
    fi
    kill -0 "$guest_pid" 2>/dev/null || { wait "$guest_pid" || true; return 1; }
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
  for member in canonical.ppm milestone10-screen.ppm milestone10-screen-after-vt.ppm frames.jsonl scene.jsonl milestone10-drm-report.jsonl milestone10-kms-before.json milestone10-kms-active.json milestone10-kms-after.json milestone10-vt-before.json milestone10-vt-active.json milestone10-vt-after.json pinned-client-result.json SHA256SUMS; do
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
required='strict_tests source_layout_audit source_layout_budget refactor_parity sanitizer headless_no_libdrm dual_backend drm_only historical_components m4_m9_regressions initial_modeset page_flip delayed_ack delayed_release hash_parity screenshot_equal vt_release vt_acquire remodeset post_vt_screenshot_equal kms_restore kd_restore vt_mode_restore active_vt_restore getty_restore device_exclusivity service_results socket_cleanup archive_validation journal_evidence'.split()
identity={'api_version':'0.5.0','soversion':'0','wire_version':'1.0','drm_mode':'1024x768','x_servers_absent':'true','mesa_absent':'true','libinput_absent':'true'}
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
errors += [f'{k} must be {v}' for k,v in identity.items() if facts.get(k)!=v]
for k in ('drm_primary_node','drm_driver','drm_connector','drm_crtc','drm_primary_plane','dumb_buffer','atomic_capability','drm_api','canonical_hash','scanout_hash','mirror_hash','screenshot_hash','compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','libdrm_version'):
  if facts.get(k) in (None,'','unknown','unavailable'): errors.append(f'{k} must be recorded')
if facts.get('canonical_hash') != facts.get('scanout_hash'): errors.append('canonical and scanout hashes differ')
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
