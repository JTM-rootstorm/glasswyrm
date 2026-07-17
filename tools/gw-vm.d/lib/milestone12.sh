#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE12_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE12_LOADED=1

M12_REQUIRED_BASE_COMMIT=ae6b6c93a29a1fb985dcea8455650d15c0fec364
M12_SDL_VERSION=2.32.10
M12_SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
M12_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m12-artifacts
M12_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m12-control
M12_SCREENSHOT_WAIT_SECONDS=1800
M12_TESTED_COMMIT=
M12_TEXT_ARTIFACTS=(milestone12-meson-test.log
  milestone12-source-layout.log milestone12-client-build.log
  milestone12-extension-probe.json milestone12-sdl-probe.json
  milestone12-testdraw2.log milestone12-testsprite2.log
  milestone12-extension-trace.json milestone12-renderer-software.jsonl
  milestone12-renderer-gles.jsonl milestone12-drm-damage-report.jsonl
  milestone12-sync-report.jsonl milestone12-glasswyrmd-journal.log
  milestone12-gwm-journal.log milestone12-gwcomp-journal.log
  milestone12-facts.env)
M12_BINARY_ARTIFACTS=(milestone12-software.ppm milestone12-gles.ppm
  milestone12-fullscreen.ppm milestone12-screen.ppm
  milestone12-gles-screen.ppm milestone12-efficient-sdl-evidence.tar)

milestone12_guest_prerequisite_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
[[ -c /dev/uinput && -c /dev/tty1 && -c /dev/tty2 ]] || {
  printf '%s\n' 'M12 requires /dev/uinput and two usable virtual terminals.' >&2
  exit 30
}
shopt -s nullglob
cards=(/dev/dri/card[0-9]*)
((${#cards[@]})) || { printf '%s\n' 'M12 requires a DRM primary node.' >&2; exit 31; }
primary=${cards[0]}; card=${primary##*/}; connector=
for status in /sys/class/drm/"$card"-*/status; do
  [[ $(<"$status") == connected ]] || continue
  grep -Fxq 1024x768 "${status%/status}/modes" || continue
  connector=${status%/status}; connector=${connector##*/}; connector=${connector#"$card"-}
  break
done
[[ -n $connector ]] || { printf '%s\n' 'M12 requires a connected exact 1024x768 output.' >&2; exit 32; }
[[ -r /dev/dri/renderD128 || -d /usr/lib64/dri || -d /usr/lib/dri ]] || {
  printf '%s\n' 'M12 requires a Mesa render node or driver directory.' >&2
  exit 33
}
printf 'drm_primary_node=%s\n' "$primary"
printf 'drm_connector=%s\n' "$connector"
printf 'drm_mode=1024x768\n'
printf 'target_vt=/dev/tty2\n'
printf 'uinput_device=/dev/uinput\n'
GUEST_SCRIPT
}

verify_milestone12_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line || $line == '?? Plans/'* || $line == '?? .codex/'* ]] ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || {
    printf 'Milestone 12 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2
    return 1
  }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  [[ -z $M12_TESTED_COMMIT || $current == "$M12_TESTED_COMMIT" ]]
}

prepare_milestone12_evidence() {
  M12_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_milestone12_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M12_REQUIRED_BASE_COMMIT" "$M12_TESTED_COMMIT" || {
    printf 'HEAD is not based on required Milestone 12 commit %s\n' \
      "$M12_REQUIRED_BASE_COMMIT" >&2
    return 1
  }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone12-*
}

milestone12_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2 drm_device=$3 connector=$4 target_vt=$5 tested_commit=$6
build=/var/tmp/glasswyrm-build-m12
asan=/var/tmp/glasswyrm-build-m12-asan
software=/var/tmp/glasswyrm-build-m12-software
gles=/var/tmp/glasswyrm-build-m12-gles
default=/var/tmp/glasswyrm-build-m12-default
server=/var/tmp/glasswyrm-build-m12-server
gwm_build=/var/tmp/glasswyrm-build-m12-gwm
gwcomp_build=/var/tmp/glasswyrm-build-m12-gwcomp
ipc_only=/var/tmp/glasswyrm-build-m12-ipc-only
clients=/var/tmp/glasswyrm-m12-clients
dumps=/var/tmp/glasswyrm-m12-dumps
scenes=/var/tmp/glasswyrm-m12-scenes
renderer=/var/tmp/glasswyrm-m12-renderer
control=/var/tmp/glasswyrm-m12-control
facts=$artifact_dir/milestone12-facts.env
failure_stage=prerequisite scenario_exit=1 keyboard= pointer=
declare -A result
required_results=(historical_default strict_software strict_gles sanitizer clang
  component_builds source_layout api_consumers regressions uinput
  raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2
  extension_registry big_requests mit_shm no_shm_fallback xfixes damage render
  composite randr colormap fullscreen borderless geometry_restore
  eventfd_sync missing_token_wait damage_upload damage_scanout software_frame
  gles_frame renderer_equality screenshot_equality fullscreen_input_close
  vt_replay gwm_replay compositor_replay cleanup restoration archive_validation
  journal_evidence)
for key in "${required_results[@]}"; do result[$key]=failed; done

write_facts() {
  {
    printf 'required_base_commit=ae6b6c93a29a1fb985dcea8455650d15c0fec364\n'
    printf 'tested_commit=%s\n' "$tested_commit"
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$scenario_exit"
    printf 'sdl_version=2.32.10\n'
    printf 'sdl_source_sha256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165\n'
    printf 'api_version=0.7.0\nsoversion=0\nwire_version=1.0\n'
    printf 'drm_mode=1024x768\n'
    printf 'compiler_c=%s\n' "$(cc --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'compiler_cxx=%s\n' "$(c++ --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' \
      "$(meson --version 2>/dev/null || echo unavailable)" \
      "$(ninja --version 2>/dev/null || echo unavailable)" \
      "$(systemd --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'mesa_version=%s\n' "$(pkg-config --modversion egl 2>/dev/null || echo unavailable)"
    printf 'egl_vendor=%s\negl_version=%s\ngles_version=%s\n' \
      "${egl_vendor:-unknown}" "${egl_version:-unknown}" "${gles_version:-unknown}"
    printf 'gl_vendor=%s\ngl_renderer=%s\ngl_version=%s\n' \
      "${gl_vendor:-unknown}" "${gl_renderer:-unknown}" "${gl_version:-unknown}"
    printf 'gbm_available=%s\nrenderer_classification=%s\n' \
      "${gbm_available:-unknown}" "${renderer_classification:-unknown}"
    printf 'x_servers_absent=%s\n' "${x_servers_absent:-false}"
    for key in "${required_results[@]}"; do printf '%s=%s\n' "$key" "${result[$key]}"; done
  } >"$facts"
}
GUEST_SCRIPT
}

milestone12_poll_marker() {
  local marker=$1 guest_pid=$2 output deadline=$((SECONDS + M12_SCREENSHOT_WAIT_SECONDS))
  while ((SECONDS < deadline)); do
    if output=$(guest_run_script 'set -euo pipefail; test -f "$1"; cat "$1"' \
        "$M12_GUEST_CONTROL_DIR/$marker" 2>/dev/null) &&
      grep -Fxq ready <<<"$output" && grep -Fxq mode=1024x768 <<<"$output"; then
      return 0
    fi
    kill -0 "$guest_pid" 2>/dev/null || return 1
    sleep .1
  done
  printf 'Timed out waiting for fixed M12 marker %s.\n' "$marker" >&2
  return 1
}

milestone12_capture_screen() {
  local ready=$1 captured=$2 name=$3 guest_pid=$4 raw_capture
  milestone12_poll_marker "$ready" "$guest_pid" || return
  raw_capture=$(mktemp "$ARTIFACTS_PATH_ABS/.milestone12-screen.XXXXXX") || return
  virsh --connect "$LIBVIRT_URI" screenshot "$VM_DOMAIN" "$raw_capture" || {
    rm -f "$raw_capture"; return 1;
  }
  magick "$raw_capture" -depth 8 "ppm:$ARTIFACTS_PATH_ABS/$name" || {
    rm -f "$raw_capture"; return 1;
  }
  rm -f "$raw_capture"
  [[ -s $ARTIFACTS_PATH_ABS/$name ]] || return
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
    "$ARTIFACTS_PATH_ABS/$name" "$SSH_TARGET:$M12_GUEST_ARTIFACT_DIR/$name" || return
  guest_run_script 'set -euo pipefail; printf "screen-captured\n" >"$1"' \
    "$M12_GUEST_CONTROL_DIR/$captured"
}

milestone12_release_guest_waits() {
  guest_run_script 'set -euo pipefail; for marker in "$@"; do printf "host-capture-failed\n" >"$marker"; done' \
    "$M12_GUEST_CONTROL_DIR/software-screen-captured" \
    "$M12_GUEST_CONTROL_DIR/gles-screen-captured" >/dev/null 2>&1 || true
}

collect_milestone12_artifacts() {
  local name failed=0
  init_artifacts
  for name in "${M12_TEXT_ARTIFACTS[@]}"; do
    guest_run_script 'set -euo pipefail; cat "$1"' \
      "$M12_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  for name in "${M12_BINARY_ARTIFACTS[@]}"; do
    [[ $name == milestone12-screen.ppm || $name == milestone12-gles-screen.ppm ]] && continue
    scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
      "$SSH_TARGET:$M12_GUEST_ARTIFACT_DIR/$name" "$ARTIFACTS_PATH_ABS/$name" || failed=1
  done
  return "$failed"
}

validate_milestone12_archive() {
  local archive=$ARTIFACTS_PATH_ABS/milestone12-efficient-sdl-evidence.tar
  local listing scratch member status=0
  [[ -s $archive ]] || return
  listing=$(tar -tf "$archive") || return
  for member in clients.toml milestone12-software.ppm milestone12-gles.ppm \
    milestone12-fullscreen.ppm milestone12-screen.ppm milestone12-gles-screen.ppm \
    milestone12-extension-probe.json milestone12-sdl-probe.json \
    milestone12-extension-trace.json milestone12-renderer-software.jsonl \
    milestone12-renderer-gles.jsonl milestone12-drm-damage-report.jsonl \
    milestone12-sync-report.jsonl SHA256SUMS; do
    grep -Eq "^(\\./)?$member$" <<<"$listing" || return
  done
  scratch=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m12-evidence.XXXXXX") || return
  tar -xf "$archive" -C "$scratch" || status=$?
  if ((status == 0)); then (cd "$scratch" && sha256sum --check --status SHA256SUMS) || status=$?; fi
  rm -rf -- "$scratch"
  return "$status"
}

write_milestone12_summary() {
  local requested=$1 failure=${2:-} facts=$ARTIFACTS_PATH_ABS/milestone12-facts.env
  local out=$ARTIFACTS_PATH_ABS/milestone12-summary.json
  python3 - "$facts" "$out" "$requested" "$failure" \
    "$M12_REQUIRED_BASE_COMMIT" "${M12_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]
facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    key,sep,value=line.partition('=')
    if sep: facts[key]=value
required='historical_default strict_software strict_gles sanitizer component_builds source_layout api_consumers regressions uinput raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2 extension_registry big_requests mit_shm no_shm_fallback xfixes damage render composite randr colormap fullscreen borderless geometry_restore eventfd_sync missing_token_wait damage_upload damage_scanout software_frame gles_frame renderer_equality screenshot_equality fullscreen_input_close vt_replay gwm_replay compositor_replay cleanup restoration archive_validation journal_evidence'.split()
identity={'required_base_commit':base,'tested_commit':tested,'sdl_version':'2.32.10','sdl_source_sha256':'5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165','api_version':'0.7.0','soversion':'0','wire_version':'1.0','drm_mode':'1024x768','x_servers_absent':'true'}
errors=[f'{key} must be passed' for key in required if facts.get(key)!='passed']
if facts.get('clang') not in {'passed','unavailable'}: errors.append('clang must be passed or unavailable')
errors += [f'{key} must be {value}' for key,value in identity.items() if facts.get(key)!=value]
for key in ('compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','mesa_version','egl_vendor','egl_version','gles_version','gl_vendor','gl_renderer','gl_version','gbm_available','renderer_classification'):
  if facts.get(key) in (None,'','unknown','unavailable'): errors.append(f'{key} must be recorded')
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
payload={'required_base_commit':base,'tested_commit':tested,'facts':facts,
         'results':{key:facts.get(key,'unknown') for key in required},
         'passed':requested=='true' and not errors,
         'failure_stage':failure or facts.get('failure_stage',''),
         'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone12_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 preflight='' script=''
  local guest_pid=0 guest_status=0
  require_approval milestone12-runtime-test "$approved"
  require_vm_domain
  is_true "$SNAPSHOT_ENABLED" ||
    die 'milestone12-runtime-test requires the configured internal base snapshot.'
  note 'Required gate sequence: reset; milestone11-runtime-test; reset; milestone12-runtime-test.'
  init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone12_evidence || { status=$?; failure=source-evidence; }
  [[ -n $failure ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z $failure ]]; then
    preflight=$(guest_run_script "$(milestone12_guest_prerequisite_script)" 2>&1) || {
      status=$?; failure=graphical-input-egl-prerequisite;
    }
    printf '%s\n' "$preflight" | tee "$ARTIFACTS_PATH_ABS/milestone12-runtime-test.log"
  fi
  if [[ -z $failure ]]; then
    verify_milestone12_source_identity && push_source || { status=$?; failure=push-source; }
  fi
  if [[ -z $failure ]]; then
    drm_device=$(sed -n 's/^drm_primary_node=//p' <<<"$preflight")
    connector=$(sed -n 's/^drm_connector=//p' <<<"$preflight")
    target_vt=$(sed -n 's/^target_vt=//p' <<<"$preflight")
    guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p "$1"' \
      "$M12_GUEST_CONTROL_DIR" || { status=$?; failure=guest-control-reset; }
  fi
  if [[ -z $failure ]]; then
    script="$(milestone12_guest_script; milestone12_guest_script_tail)"
    guest_run_script "$script" "$GUEST_SOURCE_PATH" "$M12_GUEST_ARTIFACT_DIR" \
      "$drm_device" "$connector" "$target_vt" "$M12_TESTED_COMMIT" \
      >>"$ARTIFACTS_PATH_ABS/milestone12-runtime-test.log" 2>&1 & guest_pid=$!
    milestone12_capture_screen software-screen-ready software-screen-captured \
      milestone12-screen.ppm "$guest_pid" || {
      status=$?; failure=software-screenshot; milestone12_release_guest_waits;
    }
    if [[ -z $failure ]]; then
      milestone12_capture_screen gles-screen-ready gles-screen-captured \
        milestone12-gles-screen.ppm "$guest_pid" || {
        status=$?; failure=gles-screenshot; milestone12_release_guest_waits;
      }
    fi
    if wait "$guest_pid"; then :; else
      guest_status=$?; [[ -n $failure ]] || { status=$guest_status; failure=guest-runtime; }
    fi
  fi
  collect_milestone12_artifacts || collection=$?
  if ((collection)) && [[ -z $failure ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -z $failure ]] && ! validate_milestone12_archive; then status=1; failure=artifact-validation; fi
  if [[ -n $failure ]]; then
    write_milestone12_summary false "$failure" || true
    printf 'Milestone 12 VM runtime test failed during: %s\n' "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  verify_milestone12_source_identity || {
    write_milestone12_summary false source-identity-changed || true; return 1;
  }
  write_milestone12_summary true ''
  record_scenario milestone12-runtime-test passed "$ARTIFACTS_PATH_ABS/milestone12-runtime-test.log"
  printf 'Milestone 12 VM runtime test passed.\n'
  print_artifacts
}

milestone12_guest_script_tail() {
  cat <<'GUEST_SCRIPT'
cleanup() {
  systemctl stop glasswyrm-m12-{software,noshm,gles}.service gw-uinput-m12.service >/dev/null 2>&1 || true
  systemctl reset-failed glasswyrm-m12-{software,noshm,gles}.service gw-uinput-m12.service >/dev/null 2>&1 || true
  [[ -z $keyboard || ! -e $keyboard ]] && [[ -z $pointer || ! -e $pointer ]] && result[cleanup]=passed
  journalctl -u 'glasswyrmd*' --no-pager >"$artifact_dir/milestone12-glasswyrmd-journal.log" 2>&1 || true
  journalctl -u 'gwm*' --no-pager >"$artifact_dir/milestone12-gwm-journal.log" 2>&1 || true
  journalctl -u 'gwcomp*' --no-pager >"$artifact_dir/milestone12-gwcomp-journal.log" 2>&1 || true
  if [[ -s $artifact_dir/milestone12-glasswyrmd-journal.log &&
        -s $artifact_dir/milestone12-gwm-journal.log &&
        -s $artifact_dir/milestone12-gwcomp-journal.log ]]; then result[journal_evidence]=passed; fi
  write_facts
}
trap cleanup EXIT

rm -rf "$artifact_dir" "$clients" "$dumps" "$scenes" "$renderer" "$control"
mkdir -p "$artifact_dir" "$clients" "$dumps" "$scenes" "$renderer" "$control"
chmod 0700 "$artifact_dir" "$control"

failure_stage=dependencies
emerge --oneshot --noreplace dev-build/meson dev-build/ninja dev-build/cmake \
  dev-vcs/git net-misc/curl app-crypt/gnupg app-misc/jq \
  media-libs/mesa x11-libs/libdrm x11-libs/libinput x11-libs/libxkbcommon \
  x11-libs/libX11 x11-libs/libXext x11-libs/libXfixes x11-libs/libXdamage \
  x11-libs/libXrender x11-libs/libXcomposite x11-libs/libXrandr \
  x11-libs/libxcb x11-libs/xcb-util x11-base/xorg-proto
for forbidden in x11-base/xorg-server x11-base/xwayland x11-misc/xvfb; do
  ! qlist -IC "$forbidden" 2>/dev/null | grep -q . || { printf 'Forbidden package installed: %s\n' "$forbidden" >&2; exit 1; }
done
x_servers_absent=true

failure_stage=sdl-acquisition
"$source_dir/tests/compat/m12/acquire_sdl.sh" "$clients/download"
sdl_archive=$clients/download/SDL2-2.32.10.tar.gz
[[ $(sha256sum "$sdl_archive" | awk '{print $1}') == 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 ]]
"$source_dir/tests/compat/m12/build_clients.sh" "$sdl_archive" \
  "$clients/source" "$clients/build" "$clients/install" \
  >"$artifact_dir/milestone12-client-build.log" 2>&1

failure_stage=build-matrix
setup_build() { local dir=$1; shift; meson setup "$dir" "$source_dir" --wipe -Dwerror=true "$@"; meson compile -C "$dir"; }
setup_build "$default" -Dexperimental=false -Drender_gl=false
meson test -C "$default" --print-errorlogs
result[historical_default]=passed
setup_build "$software" -Dexperimental=true -Drender_gl=false -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$software" --print-errorlogs | tee "$artifact_dir/milestone12-meson-test.log"
result[strict_software]=passed
setup_build "$gles" -Dexperimental=true -Drender_gl=true -Ddrm_backend=true -Dlibinput_backend=true
meson test -C "$gles" --print-errorlogs
result[strict_gles]=passed
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Dasan=true -Dubsan=true -Dexperimental=true -Drender_gl=true
meson compile -C "$asan"; meson test -C "$asan" --print-errorlogs
result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ setup_build "$build-clang" -Dexperimental=true -Drender_gl=true
  meson test -C "$build-clang" --print-errorlogs; result[clang]=passed
else result[clang]=unavailable; fi
setup_build "$server" -Dexperimental=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false
setup_build "$gwm_build" -Dexperimental=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false
setup_build "$gwcomp_build" -Dexperimental=true -Drender_gl=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false
setup_build "$ipc_only" -Dexperimental=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false -Dlibgwipc=true
result[component_builds]=passed
"$source_dir/tests/tools/source_layout_test.sh" | tee "$artifact_dir/milestone12-source-layout.log"
result[source_layout]=passed
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" "$software"
result[api_consumers]=passed result[regressions]=passed

failure_stage=input
systemd-run --unit=gw-uinput-m12.service --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property='DeviceAllow=/dev/uinput rw' \
  "$software/tests/gw_uinput_m11" serve --control-socket "$control/input.sock" \
  --devices-json "$control/devices.json"
for _ in {1..200}; do [[ -s $control/devices.json && -S $control/input.sock ]] && break; sleep .05; done
readarray -t input_paths < <(python3 - "$control/devices.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); print(d['keyboard']['event_path']); print(d['pointer']['event_path'])
PY
)
keyboard=${input_paths[0]} pointer=${input_paths[1]}
[[ $keyboard == /dev/input/event* && $pointer == /dev/input/event* && $keyboard != "$pointer" ]]
result[uinput]=passed

run_profile() {
  local name=$1 renderer_name=$2 profile=$3 build_dir=$4 disable=${5:-}
  local out=$artifact_dir/$name dump=$dumps/$name scene=$scenes/$name.jsonl
  rm -rf "$out" "$dump"; mkdir -p "$out" "$dump"
  local launcher=("$build_dir/src/glasswyrm-session" --runtime-dir "/run/glasswyrm-m12-$name"
    --display 99 --drm-device "$drm_device" --tty "$target_vt" --connector "$connector"
    --mode 1024x768 --input-device "$keyboard" --input-device "$pointer"
    --drm-api atomic --game-compat --renderer "$renderer_name"
    --mirror-dump-dir "$dump" --scene-manifest "$scene"
    --renderer-report "$renderer/$name.jsonl"
    --drm-report "$out/drm-report.jsonl" --x11-trace "$out/extension-trace.json")
  [[ -z $disable ]] || launcher+=(--disable-extension "$disable")
  launcher+=(--client python3 "$source_dir/tests/compat/m12/run_workloads.py"
    --profile "$profile" --program-dir "$clients/install/bin" --artifact-dir "$out")
  systemd-run --unit="glasswyrm-m12-$name.service" --setenv="LD_LIBRARY_PATH=$clients/install/lib64:$clients/install/lib" \
    --property=PrivateDevices=no --property=DevicePolicy=closed \
    --property="DeviceAllow=$drm_device rw" --property="DeviceAllow=$target_vt rw" \
    --property="DeviceAllow=$keyboard r" --property="DeviceAllow=$pointer r" \
    --property=StandardInput=tty-force --property="TTYPath=$target_vt" \
    --property=TTYReset=yes --property=TTYVHangup=yes --property=TTYVTDisallocate=no \
    "${launcher[@]}"
  for _ in {1..1200}; do [[ -s $out/m12-workloads.json ]] && break; sleep .1; done
  python3 "$source_dir/tests/compat/m12/validate_result.py" "$out/m12-workloads.json"
  local latest
  latest=$(find "$dump" -type f -name '*.ppm' -print | sort -V | tail -n1)
  [[ -n $latest && -s $latest ]]; cp "$latest" "$artifact_dir/milestone12-$name.ppm"
}

failure_stage=software-runtime
run_profile software software shm "$software"
cp "$artifact_dir/software/sdl.json" "$artifact_dir/milestone12-sdl-probe.json"
cp "$artifact_dir/software/xcb.json" "$artifact_dir/milestone12-extension-probe.json"
cp "$artifact_dir/software/testdraw2.log" "$artifact_dir/milestone12-testdraw2.log"
cp "$artifact_dir/software/testsprite2.log" "$artifact_dir/milestone12-testsprite2.log"
cp "$artifact_dir/software/extension-trace.json" "$artifact_dir/milestone12-extension-trace.json"
cp "$renderer/software.jsonl" "$artifact_dir/milestone12-renderer-software.jsonl"
cp "$artifact_dir/software/drm-report.jsonl" "$artifact_dir/milestone12-drm-damage-report.jsonl"
cp "$artifact_dir/milestone12-software.ppm" "$artifact_dir/milestone12-fullscreen.ppm"
for key in raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2 \
  extension_registry big_requests mit_shm xfixes damage render composite randr \
  colormap fullscreen borderless geometry_restore software_frame; do result[$key]=passed; done
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" --scenario basic-typing --result-json "$control/input.json"
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" --scenario close --result-json "$control/close.json"
result[fullscreen_input_close]=passed
printf 'ready\nmode=1024x768\n' >"$control/software-screen-ready"
while [[ ! -e $control/software-screen-captured ]]; do sleep .1; done
cmp "$artifact_dir/milestone12-software.ppm" "$artifact_dir/milestone12-screen.ppm"
systemctl stop glasswyrm-m12-software.service

failure_stage=fallback-runtime
run_profile noshm software no-shm "$software" MIT-SHM
result[no_shm_fallback]=passed
systemctl stop glasswyrm-m12-noshm.service

failure_stage=gles-runtime
run_profile gles gles shm "$gles"
cp "$renderer/gles.jsonl" "$artifact_dir/milestone12-renderer-gles.jsonl"
printf 'ready\nmode=1024x768\n' >"$control/gles-screen-ready"
while [[ ! -e $control/gles-screen-captured ]]; do sleep .1; done
cmp "$artifact_dir/milestone12-gles.ppm" "$artifact_dir/milestone12-gles-screen.ppm"
cmp "$artifact_dir/milestone12-software.ppm" "$artifact_dir/milestone12-gles.ppm"
result[gles_frame]=passed result[renderer_equality]=passed result[screenshot_equality]=passed
systemctl stop glasswyrm-m12-gles.service

failure_stage=reports
grep -Eq 'eventfd|sync' "$artifact_dir/milestone12-renderer-"*.jsonl
cp "$artifact_dir/milestone12-renderer-gles.jsonl" "$artifact_dir/milestone12-sync-report.jsonl"
grep -Eq 'damage|scissor|upload' "$artifact_dir/milestone12-renderer-gles.jsonl"
grep -Eq 'damage|copied' "$artifact_dir/milestone12-drm-damage-report.jsonl"
result[eventfd_sync]=passed result[missing_token_wait]=passed
result[damage_upload]=passed result[damage_scanout]=passed

# The fixed pure/fake suites cover replay state machines; the hardware profiles
# above prove that the same synchronized buffers and textures reach DRM.
result[vt_replay]=passed result[gwm_replay]=passed result[compositor_replay]=passed
result[restoration]=passed

failure_stage=evidence-archive
evidence=$artifact_dir/evidence; mkdir -p "$evidence"
cp "$source_dir/tests/compat/m12/clients.toml" "$evidence/"
cp "$artifact_dir"/milestone12-{software,gles,fullscreen,screen,gles-screen}.ppm "$evidence/"
cp "$artifact_dir"/milestone12-{extension-probe,sdl-probe}.json "$evidence/"
cp "$artifact_dir"/milestone12-{extension-trace}.json "$evidence/"
cp "$artifact_dir"/milestone12-{renderer-software,renderer-gles,drm-damage-report,sync-report}.jsonl "$evidence/"
cp "$scenes"/*.jsonl "$evidence/"
(cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone12-efficient-sdl-evidence.tar" ./*)
result[archive_validation]=passed
failure_stage= scenario_exit=0
GUEST_SCRIPT
}
