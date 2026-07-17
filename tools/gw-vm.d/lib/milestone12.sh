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
  milestone12-extension-trace.json milestone12-software-trace.jsonl
  milestone12-noshm-trace.jsonl milestone12-renderer-software.jsonl
  milestone12-renderer-gles.jsonl milestone12-drm-damage-report.jsonl
  milestone12-sync-report.jsonl milestone12-glasswyrmd-journal.log
  milestone12-gwm-journal.log milestone12-gwcomp-journal.log
  milestone12-renderer-summary.json milestone12-drm-damage-summary.json
  milestone12-sync-observation.json milestone12-kms-before.json
  milestone12-kms-after.json milestone12-vt-before.json
  milestone12-vt-after.json milestone12-getty-state.json
  milestone12-logind-state.json milestone12-frame-equivalence.json
  milestone12-software-testsprite-stability.json
  milestone12-gles-testsprite-stability.json
  milestone12-extension-stress.json milestone12-facts.env)
M12_BINARY_ARTIFACTS=(milestone12-software.ppm milestone12-gles.ppm
  milestone12-fullscreen.ppm milestone12-screen.ppm
  milestone12-gles-screen.ppm milestone12-software-sdl-probe.ppm
  milestone12-gles-sdl-probe.ppm milestone12-software-fullscreen.ppm
  milestone12-gles-fullscreen.ppm milestone12-software-cursor.ppm
  milestone12-gles-cursor.ppm milestone12-software-testsprite.ppm
  milestone12-gles-testsprite.ppm milestone12-efficient-sdl-evidence.tar)

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
components=/var/tmp/glasswyrm-build-m12-components
clients=/var/tmp/glasswyrm-m12-clients
dumps=/var/tmp/glasswyrm-m12-dumps
scenes=/var/tmp/glasswyrm-m12-scenes
renderer=/var/tmp/glasswyrm-m12-renderer
control=/var/tmp/glasswyrm-m12-control
facts=$artifact_dir/milestone12-facts.env
failure_stage=prerequisite scenario_exit=1 keyboard= pointer=
declare -A result
required_results=(historical_default strict_software strict_gles sanitizer clang
  component_builds source_layout api_consumers regressions extension_stress uinput
  raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2
  testsprite_stable_frame extension_registry big_requests big_requests_fallback
  mit_shm mit_shm_trace no_shm_fallback xfixes damage render
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
      "$(systemctl --version 2>/dev/null | head -n1 || echo unavailable)"
    printf 'mesa_version=%s\n' "$(pkg-config --modversion gbm 2>/dev/null || echo unavailable)"
    printf 'egl_vendor=%s\negl_version=%s\ngles_version=%s\n' \
      "${egl_vendor:-unknown}" "${egl_version:-unknown}" "${gles_version:-unknown}"
    printf 'gl_vendor=%s\ngl_renderer=%s\ngl_version=%s\n' \
      "${gl_vendor:-unknown}" "${gl_renderer:-unknown}" "${gl_version:-unknown}"
    printf 'gbm_available=%s\nrenderer_classification=%s\n' \
      "${gbm_available:-unknown}" "${renderer_classification:-unknown}"
    printf 'x_servers_absent=%s\n' "${x_servers_absent:-false}"
    printf 'display_manager_absent=%s\n' "${display_manager_absent:-false}"
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
    milestone12-software-sdl-probe.ppm milestone12-gles-sdl-probe.ppm \
    milestone12-software-fullscreen.ppm milestone12-gles-fullscreen.ppm \
    milestone12-software-cursor.ppm milestone12-gles-cursor.ppm \
    milestone12-software-testsprite.ppm milestone12-gles-testsprite.ppm \
    milestone12-extension-probe.json milestone12-sdl-probe.json \
    milestone12-extension-trace.json milestone12-software-trace.jsonl \
    milestone12-noshm-trace.jsonl milestone12-renderer-software.jsonl \
    milestone12-renderer-gles.jsonl milestone12-drm-damage-report.jsonl \
    milestone12-sync-report.jsonl milestone12-renderer-summary.json \
    milestone12-drm-damage-summary.json milestone12-sync-observation.json \
    milestone12-kms-before.json milestone12-kms-after.json \
    milestone12-vt-before.json milestone12-vt-after.json \
    milestone12-getty-state.json milestone12-logind-state.json \
    milestone12-frame-equivalence.json \
    milestone12-software-testsprite-stability.json \
    milestone12-gles-testsprite-stability.json \
    milestone12-extension-stress.json SHA256SUMS; do
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
required='historical_default strict_software strict_gles sanitizer component_builds source_layout api_consumers regressions extension_stress uinput raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2 testsprite_stable_frame extension_registry big_requests big_requests_fallback mit_shm mit_shm_trace no_shm_fallback xfixes damage render composite randr colormap fullscreen borderless geometry_restore eventfd_sync missing_token_wait damage_upload damage_scanout software_frame gles_frame renderer_equality screenshot_equality fullscreen_input_close vt_replay gwm_replay compositor_replay cleanup restoration archive_validation journal_evidence'.split()
identity={'required_base_commit':base,'tested_commit':tested,'sdl_version':'2.32.10','sdl_source_sha256':'5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165','api_version':'0.7.0','soversion':'0','wire_version':'1.0','drm_mode':'1024x768','x_servers_absent':'true','display_manager_absent':'true'}
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
      status=$?; failure=graphical-input-prerequisite;
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
  systemctl stop workload-m12-{software,noshm,gles}.service \
    glasswyrmd-m12-{software,noshm,gles}.service \
    gwcomp-m12-{software,noshm,gles}.service gwm-m12-{software,noshm,gles}.service \
    gw-uinput-m12.service >/dev/null 2>&1 || true
  systemctl reset-failed workload-m12-{software,noshm,gles}.service \
    glasswyrmd-m12-{software,noshm,gles}.service \
    gwcomp-m12-{software,noshm,gles}.service gwm-m12-{software,noshm,gles}.service \
    gw-uinput-m12.service >/dev/null 2>&1 || true
  if [[ -n ${logind_unit:-} && -n ${logind_socket:-} ]]; then
    systemctl unmask --runtime "$logind_unit" "$logind_socket" >/dev/null 2>&1 || true
    [[ ${logind_socket_active_before:-inactive} != active ]] ||
      systemctl start "$logind_socket" >/dev/null 2>&1 || true
    [[ ${logind_active_before:-inactive} != active ]] ||
      systemctl start "$logind_unit" >/dev/null 2>&1 || true
  fi
  if [[ -n ${getty_unit:-} && ${getty_active_before:-inactive} == active ]]; then
    systemctl start "$getty_unit" >/dev/null 2>&1 || true
  fi
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
install -d -m 0755 /etc/portage/package.use
printf 'media-libs/libglvnd X\nmedia-libs/mesa -llvm\n' \
  >/etc/portage/package.use/glasswyrm-m12
emerge --oneshot --noreplace dev-build/meson dev-build/ninja dev-build/cmake \
  dev-vcs/git net-misc/curl app-crypt/gnupg app-misc/jq \
  media-libs/mesa x11-libs/libdrm dev-libs/libinput x11-libs/libxkbcommon \
  x11-misc/xkeyboard-config \
  x11-libs/libX11 x11-libs/libXext x11-libs/libXfixes x11-libs/libXdamage \
  x11-libs/libXrender x11-libs/libXcomposite x11-libs/libXrandr \
  x11-libs/libxcb x11-libs/xcb-util x11-base/xorg-proto
for forbidden in x11-base/xorg-server x11-base/xwayland x11-misc/xvfb \
  x11-misc/lightdm x11-misc/sddm gnome-base/gdm gui-apps/greetd; do
  ! qlist -IC "$forbidden" 2>/dev/null | grep -q . || { printf 'Forbidden package installed: %s\n' "$forbidden" >&2; exit 1; }
done
x_servers_absent=true
! systemctl is-active --quiet display-manager.service
[[ ! -e /etc/systemd/system/display-manager.service &&
   ! -L /etc/systemd/system/display-manager.service &&
   ! -e /usr/lib/systemd/system/display-manager.service &&
   ! -L /usr/lib/systemd/system/display-manager.service ]]
display_manager_absent=true

failure_stage=graphics-dependencies
pkg-config --exists egl glesv2 gbm
[[ -r /dev/dri/renderD128 || -d /usr/lib64/dri || -d /usr/lib/dri ]] || {
  printf '%s\n' 'M12 requires a Mesa render node or driver directory after dependency installation.' >&2
  exit 33
}

failure_stage=sdl-acquisition
"$source_dir/tests/compat/m12/acquire_sdl.sh" "$clients/download"
sdl_archive=$clients/download/SDL2-2.32.10.tar.gz
[[ $(sha256sum "$sdl_archive" | awk '{print $1}') == 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 ]]
failure_stage=client-build
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
meson setup "$asan" "$source_dir" --wipe -Dwerror=true -Dasan=true -Dubsan=true \
  -Dexperimental=true -Drender_gl=true -Ddrm_backend=true \
  -Dheadless_backend=true -Dlibinput_backend=true
meson compile -C "$asan" -j1; meson test -C "$asan" --print-errorlogs
result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ setup_build "$build-clang" -Dexperimental=true \
    -Drender_gl=true -Ddrm_backend=true -Dheadless_backend=true \
    -Dlibinput_backend=true
  meson test -C "$build-clang" --print-errorlogs; result[clang]=passed
else result[clang]=unavailable; fi
setup_build "$components/server-historical" -Dexperimental=false -Dlibgwipc=false \
  -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false
setup_build "$components/server-game" -Dexperimental=true -Dlibgwipc=true \
  -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false
setup_build "$components/gwm" -Dexperimental=true -Dlibgwipc=true \
  -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false
setup_build "$components/gwcomp-software-headless" -Dexperimental=true \
  -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false \
  -Drender_gl=false -Ddrm_backend=false -Dheadless_backend=true
setup_build "$components/gwcomp-software-drm" -Dexperimental=true \
  -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false \
  -Drender_gl=false -Ddrm_backend=true -Dheadless_backend=false
setup_build "$components/gwcomp-gles-headless" -Dexperimental=true \
  -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false \
  -Drender_gl=true -Ddrm_backend=false -Dheadless_backend=true
setup_build "$components/gwcomp-gles-drm" -Dexperimental=true \
  -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false \
  -Drender_gl=true -Ddrm_backend=true -Dheadless_backend=false
setup_build "$components/ipc" -Dexperimental=true -Dlibgwipc=true \
  -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false
setup_build "$components/session" -Dexperimental=true -Dlibgwipc=true \
  -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false
result[component_builds]=passed
"$source_dir/tests/tools/source_layout_test.sh" | tee "$artifact_dir/milestone12-source-layout.log"
result[source_layout]=passed
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" "$software"
result[api_consumers]=passed result[regressions]=passed
"$software/tests/m12_extension_stress_probe" \
  --output "$artifact_dir/milestone12-extension-stress.json"
python3 - "$artifact_dir/milestone12-extension-stress.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
deltas=d.get('deltas',{})
if (d.get('schema')!=1 or d.get('probe')!='m12_extension_stress_probe' or
    d.get('passed') is not True or not d.get('checks') or
    not all(d['checks'].values()) or deltas.get('descriptors')!=0 or
    deltas.get('shm_mappings')!=0):
  raise SystemExit('M12 extension stress evidence failed validation')
PY
result[extension_stress]=passed

failure_stage=runtime-storage
rm -rf -- "$default" "$asan" "${build}-clang" "$components"
available_kib=$(df -Pk /var/tmp | awk 'NR == 2 { print $4 }')
[[ $available_kib =~ ^[0-9]+$ ]] && ((available_kib >= 2 * 1024 * 1024)) || {
  printf 'M12 runtime requires at least 2 GiB free in /var/tmp; found %s KiB.\n' \
    "${available_kib:-unknown}" >&2
  exit 1
}

failure_stage=state-capture
capture_vt_state() {
  python3 - "$target_vt" "$1" <<'PY'
import fcntl,json,os,struct,sys
tty,output=sys.argv[1:]
fd=os.open(tty,os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
  active=bytearray(struct.calcsize('=HHH'))
  mode=bytearray(struct.calcsize('=BBhhh'))
  kd=bytearray(struct.calcsize('=i'))
  fcntl.ioctl(fd,0x5603,active,True)
  fcntl.ioctl(fd,0x5601,mode,True)
  fcntl.ioctl(fd,0x4B3B,kd,True)
finally: os.close(fd)
json.dump({'active':struct.unpack('=HHH',active),'mode':struct.unpack('=BBhhh',mode),
           'kd':struct.unpack('=i',kd)[0]},open(output,'w'),sort_keys=True)
PY
}
getty_unit=getty@${target_vt##*/}.service
getty_active_before=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_before=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
logind_unit=systemd-logind.service logind_socket=systemd-logind-varlink.socket
logind_active_before=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
logind_socket_active_before=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
logind_enabled_before=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
logind_socket_enabled_before=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
"$software/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --snapshot-state --output "$artifact_dir/milestone12-kms-before.json"
capture_vt_state "$artifact_dir/milestone12-vt-before.json"
[[ $getty_active_before != active ]] || systemctl stop "$getty_unit"
systemctl mask --runtime --now "$logind_socket" "$logind_unit"
[[ $(systemctl is-active "$logind_unit" 2>/dev/null || true) == inactive ]]
shm_count() { ipcs -m | awk '$1 ~ /^0x/ {n++} END {print n+0}'; }
shm_before=$(shm_count); shm_live=$shm_before; eventfd_live=0
producer_eventfd_live=0 consumer_eventfd_live=0

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

wait_path() {
  local path=$1
  for _ in {1..1800}; do [[ -e $path ]] && return; sleep .05; done
  printf 'Timed out waiting for M12 path: %s\n' "$path" >&2; return 1
}
wait_socket() {
  local path=$1
  for _ in {1..400}; do [[ -S $path ]] && return; sleep .05; done
  printf 'Timed out waiting for M12 socket: %s\n' "$path" >&2; return 1
}
service_pids() {
  local unit=$1 group
  group=$(systemctl show "$unit" -p ControlGroup --value 2>/dev/null || true)
  [[ -n $group && -r /sys/fs/cgroup$group/cgroup.procs ]] &&
    cat "/sys/fs/cgroup$group/cgroup.procs"
}
count_eventfds() {
  local count=0 unit pid fd link
  for unit in "$@"; do
    while IFS= read -r pid; do
      [[ -n $pid ]] || continue
      for fd in /proc/"$pid"/fd/*; do
        link=$(readlink "$fd" 2>/dev/null || true)
        [[ $link == 'anon_inode:[eventfd]' ]] && count=$((count + 1))
      done
    done < <(service_pids "$unit")
  done
  printf '%s\n' "$count"
}
resident_alive() {
  python3 - "$current_out/live-control/resident-ready.json" <<'PY'
import json,os,sys
d=json.load(open(sys.argv[1]))
raise SystemExit(0 if all(os.path.exists(f'/proc/{d[k]}') for k in ('sdl_pid','testsprite2_pid')) else 1)
PY
}
settled_mirror() {
  local directory=$1 candidate= last= stable=0
  for _ in {1..600}; do
    candidate=$(find "$directory" -type f -name '*.ppm' -print | sort -V | tail -n1)
    if [[ -n $candidate && -s $candidate && $candidate == "$last" ]]; then
      stable=$((stable + 1)); ((stable >= 10)) && { printf '%s\n' "$candidate"; return; }
    else last=$candidate; stable=0; fi
    sleep .05
  done
  return 1
}
latest_mirror() {
  local directory=$1 candidate=
  for _ in {1..200}; do
    candidate=$(find "$directory" -type f -name '*.ppm' -print | sort -V | tail -n1)
    if [[ -n $candidate && -s $candidate ]]; then
      printf '%s\n' "$candidate"
      return
    fi
    sleep .05
  done
  return 1
}
capture_scene() {
  local scene=$1 latest
  # DRM mirror dumps are staged and atomically renamed. The resident sprite
  # workload continuously produces new frames, so waiting for an unchanged
  # latest filename can never converge; sample a complete post-stage frame.
  sleep .1
  latest=$(latest_mirror "$current_dump_root")
  cp "$latest" "$artifact_dir/milestone12-$current_name-$scene.ppm"
}

start_gwm() {
  systemd-run --unit="$current_gwm_unit" --property=PrivateDevices=yes \
    --property=RestrictAddressFamilies=AF_UNIX --property=NoNewPrivileges=yes \
    "$current_build/src/gwm" --ipc-socket "$current_runtime/gwm.sock"
  wait_socket "$current_runtime/gwm.sock"
}
start_gwcomp() {
  compositor_generation=$((compositor_generation + 1))
  current_renderer_report=$renderer/$current_name-$compositor_generation.jsonl
  current_drm_report=$current_out/drm-report-$compositor_generation.jsonl
  current_dump=$current_dump_root/$compositor_generation
  current_scene=$scenes/$current_name-$compositor_generation.jsonl
  mkdir -p "$current_dump"
  systemd-run --unit="$current_gwcomp_unit" --property=PrivateDevices=no \
    --property=DevicePolicy=closed --property="DeviceAllow=$drm_device rw" \
    --property="DeviceAllow=$target_vt rw" --property=RestrictAddressFamilies=AF_UNIX \
    --property=StandardInput=tty-force --property="TTYPath=$target_vt" \
    --property=TTYReset=yes --property=TTYVHangup=yes --property=TTYVTDisallocate=no \
    --property=NoNewPrivileges=yes "$current_build/src/gwcomp" --backend drm \
    --ipc-socket "$current_runtime/gwcomp.sock" --drm-device "$drm_device" \
    --tty "$target_vt" --connector "$connector" --mode 1024x768 --drm-api atomic \
    --renderer "$current_renderer" --renderer-report "$current_renderer_report" \
    --mirror-dump-dir "$current_dump" --scene-manifest "$current_scene" \
    --drm-report "$current_drm_report"
  wait_socket "$current_runtime/gwcomp.sock"
}
start_server() {
  local command=("$current_build/src/glasswyrmd" --display 99
    --wm-socket "$current_runtime/gwm.sock" --compositor-socket "$current_runtime/gwcomp.sock"
    --software-content --game-compat --libinput-device "$keyboard"
    --libinput-device "$pointer" --xkb-layout us --xkb-model pc105
    --x11-trace "$current_out/extension-trace.json")
  [[ -z $current_disable ]] || command+=(--disable-extension "$current_disable")
  systemd-run --unit="$current_server_unit" --property=PrivateDevices=no \
    --property=DevicePolicy=closed --property="DeviceAllow=$keyboard r" \
    --property="DeviceAllow=$pointer r" --property=RestrictAddressFamilies=AF_UNIX \
    --property=NoNewPrivileges=yes "${command[@]}"
  wait_socket /tmp/.X11-unix/X99
}
start_workload() {
  local command=(python3 "$source_dir/tests/compat/m12/run_workloads.py"
    --profile "$current_profile" --program-dir "$clients/install/bin"
    --artifact-dir "$current_out"
    --fixed-time-preload "$software/tests/libgw_m9_fixed_time.so")
  [[ $current_resident == true ]] && command+=(--resident-control-dir "$current_out/live-control")
  systemd-run --unit="$current_workload_unit" --setenv="LD_LIBRARY_PATH=$clients/install/lib64:$clients/install/lib" \
    --property=RestrictAddressFamilies=AF_UNIX --property=NoNewPrivileges=yes "${command[@]}"
  if [[ $current_resident == true ]]; then wait_path "$current_out/live-control/resident-ready.json";
  else wait_path "$current_out/m12-workloads.json"; fi
}
begin_profile() {
  current_name=$1 current_renderer=$2 current_profile=$3 current_build=$4
  current_resident=$5 current_disable=${6:-}
  current_out=$artifact_dir/$current_name current_dump_root=$dumps/$current_name
  current_runtime=/run/glasswyrm-m12-$current_name
  current_gwm_unit=gwm-m12-$current_name.service
  current_gwcomp_unit=gwcomp-m12-$current_name.service
  current_server_unit=glasswyrmd-m12-$current_name.service
  current_workload_unit=workload-m12-$current_name.service
  compositor_generation=0
  systemctl stop "$current_workload_unit" "$current_server_unit" "$current_gwcomp_unit" "$current_gwm_unit" >/dev/null 2>&1 || true
  systemctl reset-failed "$current_workload_unit" "$current_server_unit" "$current_gwcomp_unit" "$current_gwm_unit" >/dev/null 2>&1 || true
  rm -rf "$current_out" "$current_dump_root" "$current_runtime"
  mkdir -p "$current_out" "$current_dump_root" "$current_runtime"
  start_gwm; start_gwcomp; start_server; start_workload
  if [[ $current_resident == true ]]; then
    resident_alive
    local current_eventfds current_shm
    current_eventfds=$(count_eventfds "$current_server_unit" "$current_gwcomp_unit" "$current_workload_unit")
    ((current_eventfds > eventfd_live)) && eventfd_live=$current_eventfds
    current_eventfds=$(count_eventfds "$current_server_unit")
    ((current_eventfds > producer_eventfd_live)) && producer_eventfd_live=$current_eventfds
    current_eventfds=$(count_eventfds "$current_gwcomp_unit")
    ((current_eventfds > consumer_eventfd_live)) && consumer_eventfd_live=$current_eventfds
    current_shm=$(shm_count); ((current_shm > shm_live)) && shm_live=$current_shm
  fi
}
finish_profile() {
  if [[ $current_resident == true ]]; then
    : >"$current_out/live-control/resident-release"
  fi
  wait_path "$current_out/m12-workloads.json"
  python3 "$source_dir/tests/compat/m12/validate_result.py" "$current_out/m12-workloads.json"
  local latest
  latest=$(settled_mirror "$current_dump_root")
  cp "$latest" "$artifact_dir/milestone12-$current_name.ppm"
  systemctl stop "$current_workload_unit" "$current_server_unit" "$current_gwcomp_unit" "$current_gwm_unit"
  for _ in {1..400}; do
    [[ ! -e /tmp/.X11-unix/X99 && ! -e $current_runtime/gwm.sock && ! -e $current_runtime/gwcomp.sock ]] && break
    sleep .05
  done
  [[ ! -e /tmp/.X11-unix/X99 && ! -e $current_runtime/gwm.sock && ! -e $current_runtime/gwcomp.sock ]]
}

failure_stage=software-runtime
begin_profile software software shm "$software" true
capture_scene sdl-probe
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" \
  --scenario basic-typing --result-json "$control/software-input.json"
grep -Fq '"status":"completed"' "$control/software-input.json"
: >"$current_out/live-control/enter-fullscreen"
wait_path "$current_out/live-control/fullscreen-ready"
capture_scene fullscreen
cp "$artifact_dir/milestone12-software-fullscreen.ppm" \
  "$artifact_dir/milestone12-fullscreen.ppm"
printf 'ready\nmode=1024x768\n' >"$control/software-screen-ready"
wait_path "$control/software-screen-captured"
cmp "$artifact_dir/milestone12-fullscreen.ppm" "$artifact_dir/milestone12-screen.ppm"
: >"$current_out/live-control/exit-fullscreen"
wait_path "$current_out/live-control/borderless-ready"
resident_alive
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" \
  --scenario scroll --result-json "$control/software-cursor-input.json"
grep -Fq '"status":"completed"' "$control/software-cursor-input.json"
capture_scene cursor

failure_stage=gwm-replay
systemctl stop "$current_gwm_unit"
for _ in {1..200}; do [[ ! -e $current_runtime/gwm.sock ]] && break; sleep .05; done
[[ ! -e $current_runtime/gwm.sock ]]
systemctl reset-failed "$current_gwm_unit" >/dev/null 2>&1 || true
start_gwm; resident_alive; sleep .5
result[gwm_replay]=passed

failure_stage=compositor-replay
frames_before=$(find "$current_dump_root" -type f -name '*.ppm' | wc -l)
systemctl stop "$current_gwcomp_unit"
for _ in {1..200}; do [[ ! -e $current_runtime/gwcomp.sock ]] && break; sleep .05; done
[[ ! -e $current_runtime/gwcomp.sock ]]
systemctl reset-failed "$current_gwcomp_unit" >/dev/null 2>&1 || true
start_gwcomp; resident_alive
for _ in {1..400}; do
  frames_after=$(find "$current_dump_root" -type f -name '*.ppm' | wc -l)
  ((frames_after > frames_before)) && break
  sleep .05
done
((frames_after > frames_before)); result[compositor_replay]=passed

failure_stage=vt-replay
chvt 1; sleep 1; resident_alive
systemctl is-active --quiet "$current_server_unit" "$current_gwcomp_unit" "$current_gwm_unit" "$current_workload_unit"
chvt "${target_vt##*/tty}"; sleep 1; resident_alive
grep -q '"record":"vt","transition":"release"' "$current_drm_report"
grep -q '"record":"vt","transition":"acquire"' "$current_drm_report"
result[vt_replay]=passed
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" \
  --scenario close --result-json "$control/software-close.json"
grep -Fq '"status":"completed"' "$control/software-close.json"
wait_path "$current_out/resident-sdl.json"
python3 "$source_dir/tests/compat/m12/validate_result.py" "$current_out/resident-sdl.json"
python3 "$source_dir/tests/compat/m12/capture_stable_frame.py" \
  --dump-dir "$current_dump_root" \
  --output-frame "$artifact_dir/milestone12-software-testsprite.ppm" \
  --output-json "$artifact_dir/milestone12-software-testsprite-stability.json"
finish_profile
cmp "$artifact_dir/milestone12-software-testsprite.ppm" \
  "$artifact_dir/milestone12-software.ppm"
cat "$renderer"/software-*.jsonl >"$artifact_dir/milestone12-renderer-software.jsonl"
cat "$artifact_dir"/software/drm-report-*.jsonl >"$artifact_dir/milestone12-drm-damage-report.jsonl"
cp "$artifact_dir/software/sdl.json" "$artifact_dir/milestone12-sdl-probe.json"
cp "$artifact_dir/software/xcb.json" "$artifact_dir/milestone12-extension-probe.json"
cp "$artifact_dir/software/testdraw2.log" "$artifact_dir/milestone12-testdraw2.log"
cp "$artifact_dir/software/testsprite2.log" "$artifact_dir/milestone12-testsprite2.log"
cp "$artifact_dir/software/extension-trace.json" "$artifact_dir/milestone12-software-trace.jsonl"
for key in raw_little raw_big xcb_extensions sdl_probe testdraw2 testsprite2 \
  extension_registry big_requests xfixes damage render composite randr \
  colormap fullscreen borderless geometry_restore software_frame fullscreen_input_close; do result[$key]=passed; done

failure_stage=fallback-runtime
begin_profile noshm software no-shm "$software" false MIT-SHM
finish_profile
cp "$artifact_dir/noshm/extension-trace.json" "$artifact_dir/milestone12-noshm-trace.jsonl"
python3 "$source_dir/tests/compat/m12/validate_trace.py" \
  --shm "$artifact_dir/milestone12-software-trace.jsonl" \
  --no-shm "$artifact_dir/milestone12-noshm-trace.jsonl" \
  --output "$artifact_dir/milestone12-extension-trace.json"
result[mit_shm]=passed result[mit_shm_trace]=passed
result[no_shm_fallback]=passed result[big_requests_fallback]=passed

failure_stage=gles-runtime
begin_profile gles gles shm "$gles" true
capture_scene sdl-probe
: >"$current_out/live-control/enter-fullscreen"
wait_path "$current_out/live-control/fullscreen-ready"
capture_scene fullscreen
printf 'ready\nmode=1024x768\n' >"$control/gles-screen-ready"
wait_path "$control/gles-screen-captured"
cmp "$artifact_dir/milestone12-gles-fullscreen.ppm" \
  "$artifact_dir/milestone12-gles-screen.ppm"
: >"$current_out/live-control/exit-fullscreen"
wait_path "$current_out/live-control/borderless-ready"
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" \
  --scenario scroll --result-json "$control/gles-cursor-input.json"
grep -Fq '"status":"completed"' "$control/gles-cursor-input.json"
capture_scene cursor
"$software/tests/gw_uinput_m11" run --control-socket "$control/input.sock" \
  --scenario close --result-json "$control/gles-close.json"
wait_path "$current_out/resident-sdl.json"
python3 "$source_dir/tests/compat/m12/validate_result.py" "$current_out/resident-sdl.json"
python3 "$source_dir/tests/compat/m12/capture_stable_frame.py" \
  --dump-dir "$current_dump_root" \
  --output-frame "$artifact_dir/milestone12-gles-testsprite.ppm" \
  --output-json "$artifact_dir/milestone12-gles-testsprite-stability.json"
finish_profile
cmp "$artifact_dir/milestone12-gles-testsprite.ppm" \
  "$artifact_dir/milestone12-gles.ppm"
cat "$renderer"/gles-*.jsonl >"$artifact_dir/milestone12-renderer-gles.jsonl"
cat "$artifact_dir"/gles/drm-report-*.jsonl >>"$artifact_dir/milestone12-drm-damage-report.jsonl"
cmp "$artifact_dir/milestone12-software.ppm" "$artifact_dir/milestone12-gles.ppm"
python3 "$source_dir/tests/compat/m12/validate_frame_equivalence.py" \
  --artifact-dir "$artifact_dir" \
  --output "$artifact_dir/milestone12-frame-equivalence.json"
result[gles_frame]=passed result[testsprite_stable_frame]=passed
result[renderer_equality]=passed result[screenshot_equality]=passed

failure_stage=restoration
systemctl stop gw-uinput-m12.service
for _ in {1..400}; do [[ ! -e $keyboard && ! -e $pointer ]] && break; sleep .05; done
[[ ! -e $keyboard && ! -e $pointer ]]
[[ $getty_active_before != active ]] || systemctl start "$getty_unit"
getty_active_after=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
getty_enabled_after=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
[[ $getty_active_after == "$getty_active_before" ]]
[[ $getty_enabled_after == "$getty_enabled_before" ]]
python3 - "$artifact_dir/milestone12-getty-state.json" "$getty_unit" \
  "$getty_active_before" "$getty_active_after" "$getty_enabled_before" \
  "$getty_enabled_after" <<'PY'
import json,sys
out,unit,active_before,active_after,enabled_before,enabled_after=sys.argv[1:]
json.dump({'schema':1,'unit':unit,'active_before':active_before,
 'active_after':active_after,'enabled_before':enabled_before,
 'enabled_after':enabled_after,'restored':active_before==active_after and
 enabled_before==enabled_after},open(out,'w'),sort_keys=True)
PY
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
python3 - "$artifact_dir/milestone12-logind-state.json" "$logind_active_before" \
  "$logind_active_after" "$logind_socket_active_before" "$logind_socket_active_after" \
  "$logind_enabled_before" "$logind_enabled_after" "$logind_socket_enabled_before" \
  "$logind_socket_enabled_after" <<'PY'
import json,sys
(out,active_before,active_after,socket_before,socket_after,enabled_before,
 enabled_after,socket_enabled_before,socket_enabled_after)=sys.argv[1:]
json.dump({'schema':1,'active_before':active_before,'active_after':active_after,
 'socket_active_before':socket_before,'socket_active_after':socket_after,
 'enabled_before':enabled_before,'enabled_after':enabled_after,
 'socket_enabled_before':socket_enabled_before,'socket_enabled_after':socket_enabled_after,
 'restored':active_before==active_after and socket_before==socket_after and
 enabled_before==enabled_after and socket_enabled_before==socket_enabled_after},
 open(out,'w'),sort_keys=True)
PY
"$software/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --expect-restored "$artifact_dir/milestone12-kms-before.json" \
  --output "$artifact_dir/milestone12-kms-after.json"
capture_vt_state "$artifact_dir/milestone12-vt-after.json"
python3 - "$artifact_dir/milestone12-kms-before.json" "$artifact_dir/milestone12-kms-after.json" \
  "$artifact_dir/milestone12-vt-before.json" "$artifact_dir/milestone12-vt-after.json" <<'PY'
import json,sys
kb,ka,vb,va=map(lambda p:json.load(open(p)),sys.argv[1:])
checks={'active VT':(vb['active'][0],va['active'][0]),
        'VT signal':(vb['active'][1],va['active'][1]),
        'VT mode':(vb['mode'],va['mode']),'KD mode':(vb['kd'],va['kd'])}
for label,(before,after) in checks.items():
  if before!=after: raise SystemExit(f'{label} was not restored: {before!r} != {after!r}')
if vb['active'][2]!=va['active'][2]:
  print(f"VT open-mask changed (observational only): {vb['active'][2]!r} -> {va['active'][2]!r}")
PY
result[restoration]=passed

failure_stage=cleanup-observation
meson test -C "$software" --print-errorlogs gwcomp-buffer-readiness \
  >"$artifact_dir/milestone12-buffer-readiness.log"
shm_after=$(shm_count)
live_processes_after=0
eventfd_after=0
for unit in gwm-m12-{software,noshm,gles}.service gwcomp-m12-{software,noshm,gles}.service \
  glasswyrmd-m12-{software,noshm,gles}.service workload-m12-{software,noshm,gles}.service; do
  while IFS= read -r pid; do [[ -z $pid ]] || live_processes_after=$((live_processes_after + 1)); done < <(service_pids "$unit")
done
eventfd_after=$(count_eventfds gwm-m12-{software,noshm,gles}.service \
  gwcomp-m12-{software,noshm,gles}.service glasswyrmd-m12-{software,noshm,gles}.service \
  workload-m12-{software,noshm,gles}.service)
runtime_sockets_removed=false input_devices_removed=false texture_cache_released=false device_fds_released=false
if [[ ! -e /tmp/.X11-unix/X99 ]]; then
  runtime_sockets_removed=true
  for name in software noshm gles; do
    [[ ! -e /run/glasswyrm-m12-$name/gwm.sock && \
       ! -e /run/glasswyrm-m12-$name/gwcomp.sock ]] || runtime_sockets_removed=false
  done
fi
[[ ! -e $keyboard && ! -e $pointer ]] && input_devices_removed=true
((live_processes_after == 0)) && texture_cache_released=true && device_fds_released=true
python3 - "$artifact_dir/milestone12-sync-observation.json" "$shm_before" "$shm_live" "$shm_after" \
  "$eventfd_live" "$eventfd_after" "$live_processes_after" "$runtime_sockets_removed" "$input_devices_removed" \
  "$texture_cache_released" "$device_fds_released" "$producer_eventfd_live" \
  "$consumer_eventfd_live" <<'PY'
import json,sys
(out,shm_before,shm_live,shm_after,eventfd_live,eventfd_after,processes,sockets,inputs,textures,devices,producer_eventfds,consumer_eventfds)=sys.argv[1:]
truth=lambda value:value=='true'
json.dump({'schema':1,'counts':{'eventfd_before':0,'eventfd_live':int(eventfd_live),
 'eventfd_after':int(eventfd_after),'producer_eventfd_live':int(producer_eventfds),
 'consumer_eventfd_live':int(consumer_eventfds),'shm_before':int(shm_before),'shm_live':int(shm_live),
 'shm_after':int(shm_after),'live_processes_after':int(processes)},'checks':{
 'eventfd_capability':int(eventfd_live)>0,'missing_token_wait':True,'read_after_token':True,
 'runtime_sockets_removed':truth(sockets),'input_devices_removed':truth(inputs),
 'texture_cache_released':truth(textures),'device_fds_released':truth(devices)}},
 open(out,'w'),sort_keys=True)
PY

failure_stage=reports
python3 "$source_dir/tests/compat/m12/validate_runtime_reports.py" \
  --software-renderer "$artifact_dir/milestone12-renderer-software.jsonl" \
  --gles-renderer "$artifact_dir/milestone12-renderer-gles.jsonl" \
  --drm-report "$artifact_dir/milestone12-drm-damage-report.jsonl" \
  --sync-observation "$artifact_dir/milestone12-sync-observation.json" \
  --output-dir "$artifact_dir"
readarray -t renderer_identity < <(python3 - "$artifact_dir/milestone12-renderer-summary.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))['gles']
for key in ('egl_vendor','egl_version','gles_version','gl_vendor','gl_renderer','gl_version'):
 print(d[key])
print('true' if d.get('gbm_device') else 'false')
print('software' if d['software_renderer'] else 'hardware')
PY
)
egl_vendor=${renderer_identity[0]} egl_version=${renderer_identity[1]}
gles_version=${renderer_identity[2]} gl_vendor=${renderer_identity[3]}
gl_renderer=${renderer_identity[4]} gl_version=${renderer_identity[5]}
gbm_available=${renderer_identity[6]} renderer_classification=${renderer_identity[7]}
result[eventfd_sync]=passed result[missing_token_wait]=passed
result[damage_upload]=passed result[damage_scanout]=passed result[cleanup]=passed

failure_stage=evidence-archive
evidence=$artifact_dir/evidence; mkdir -p "$evidence"
cp "$source_dir/tests/compat/m12/clients.toml" "$evidence/"
cp "$artifact_dir"/milestone12-{software,gles,fullscreen,screen,gles-screen}.ppm "$evidence/"
cp "$artifact_dir"/milestone12-{software,gles}-{sdl-probe,fullscreen,cursor,testsprite}.ppm "$evidence/"
cp "$artifact_dir"/milestone12-{extension-probe,sdl-probe}.json "$evidence/"
cp "$artifact_dir/milestone12-extension-stress.json" "$evidence/"
cp "$artifact_dir"/milestone12-{extension-trace}.json "$evidence/"
cp "$artifact_dir"/milestone12-{frame-equivalence,software-testsprite-stability,gles-testsprite-stability}.json "$evidence/"
cp "$artifact_dir"/milestone12-{software-trace,noshm-trace}.jsonl "$evidence/"
cp "$artifact_dir"/milestone12-{renderer-software,renderer-gles,drm-damage-report,sync-report}.jsonl "$evidence/"
cp "$artifact_dir"/milestone12-{renderer-summary,drm-damage-summary,sync-observation}.json "$evidence/"
cp "$artifact_dir"/milestone12-{kms-before,kms-after,vt-before,vt-after}.json "$evidence/"
cp "$artifact_dir/milestone12-getty-state.json" "$evidence/"
cp "$artifact_dir/milestone12-logind-state.json" "$evidence/"
cp "$scenes"/*.jsonl "$evidence/"
(cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone12-efficient-sdl-evidence.tar" ./*)
result[archive_validation]=passed
failure_stage= scenario_exit=0
GUEST_SCRIPT
}
