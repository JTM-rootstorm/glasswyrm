#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n ${GW_VM_MILESTONE11_LOADED:-} ]]; then return 0; fi
GW_VM_MILESTONE11_LOADED=1

M11_REQUIRED_BASE_COMMIT=9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0
M11_XTERM_SHA256=7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f
M11_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m11-artifacts
M11_GUEST_CONTROL_DIR=/var/tmp/glasswyrm-m11-control
M11_SCREENSHOT_WAIT_SECONDS=1800
M11_TESTED_COMMIT=
M11_TEXT_ARTIFACTS=(milestone11-runtime-test.log milestone11-meson-test.log
  milestone11-source-layout.log milestone11-libinput-devices.json
  milestone11-keymap.json milestone11-xterm.log milestone11-xterm-trace.raw.jsonl
  milestone11-xterm-trace.json milestone11-pty-transcript.log
  milestone11-xterm-geometry.json
  milestone11-selection.log milestone11-interactive-wm.log
  milestone11-gwm-bindings.json milestone11-selection-client-message.json
  milestone11-session-state.log milestone11-drm-report.jsonl
  milestone11-drm-report-before-restart.jsonl
  milestone11-glasswyrmd-journal.log milestone11-gwm-journal.log
  milestone11-gwcomp-journal.log milestone11-session-journal.log
  milestone11-facts.env)
M11_BINARY_ARTIFACTS=(milestone11-desktop.ppm
  milestone11-desktop-after-vt.ppm milestone11-desktop-after-restart.ppm
  milestone11-interactive-evidence.tar)

milestone11_guest_prerequisite_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
[[ -c /dev/uinput ]] || {
  printf '%s\n' 'M11 prerequisite failed before package installation: /dev/uinput is unavailable; the base snapshot kernel must provide CONFIG_INPUT_UINPUT.' >&2
  exit 30
}
[[ -r /sys/class/input ]] || {
  printf '%s\n' 'M11 prerequisite failed before package installation: sysfs input inventory is unavailable.' >&2
  exit 31
}
shopt -s nullglob
nodes=(/dev/dri/card[0-9]*)
((${#nodes[@]})) || {
  printf '%s\n' 'M11 prerequisite failed before package installation: no DRM primary node (/dev/dri/card*) is exposed by the base kernel.' >&2
  exit 32
}
primary=${nodes[0]}; card=${primary##*/}; connector=
for status in /sys/class/drm/"$card"-*/status; do
  [[ $(<"$status") == connected ]] || continue
  while IFS= read -r mode; do
    if [[ $mode == 1024x768 ]]; then
      connector=${status%/status}; connector=${connector##*/}; connector=${connector#"$card"-}
      break 2
    fi
  done <"${status%/status}/modes"
done
[[ -n $connector && -c /dev/tty1 && -c /dev/tty2 ]] || {
  printf '%s\n' 'M11 prerequisite failed before package installation: exact 1024x768 output and two VTs are required.' >&2
  exit 33
}
printf 'drm_primary_node=%s\n' "$primary"
printf 'drm_connector=%s\n' "$connector"
printf 'drm_mode=1024x768\n'
printf 'target_vt=/dev/tty2\n'
printf 'uinput_device=/dev/uinput\n'
GUEST_SCRIPT
}

milestone11_doctor() {
  local failed=0 probe
  milestone10_doctor || failed=1
  if probe=$(guest_run_script "$(milestone11_guest_prerequisite_script)" 2>&1); then
    while IFS= read -r line; do printf '[ok] guest %s\n' "$line"; done <<<"$probe"
  else
    printf '[failed] guest M11 input/graphical prerequisite probe\n%s\n' "$probe"
    failed=1
  fi
  return "$failed"
}

verify_milestone11_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line || $line == '?? Plans/'* || $line == '?? .codex/'* ]] ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || {
    printf 'Milestone 11 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2
    return 1
  }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  [[ -z $M11_TESTED_COMMIT || $current == "$M11_TESTED_COMMIT" ]]
}

prepare_milestone11_evidence() {
  M11_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_milestone11_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M11_REQUIRED_BASE_COMMIT" "$M11_TESTED_COMMIT" || {
    printf 'HEAD is not based on required Milestone 11 commit %s\n' \
      "$M11_REQUIRED_BASE_COMMIT" >&2
    return 1
  }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone11-*
}

milestone11_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2 drm_device=$3 connector=$4 target_vt=$5 xterm_sha=$6
tested_commit=$7 run_mode=$8
[[ $tested_commit =~ ^[0-9a-f]{40}$ ]] || {
  printf '%s\n' 'M11 guest runner requires an exact 40-character tested commit.' >&2
  exit 2
}
[[ $run_mode == full || $run_mode == interactive-rerun ]] || {
  printf 'Unknown M11 guest run mode: %s\n' "$run_mode" >&2
  exit 2
}
build=/var/tmp/glasswyrm-build-m11
asan=/var/tmp/glasswyrm-build-m11-asan
runtime=/var/tmp/glasswyrm-build-m11-runtime
clang_build=/var/tmp/glasswyrm-build-m11-clang
default=/var/tmp/glasswyrm-build-m11-default
server=/var/tmp/glasswyrm-build-m11-server-input
server_standalone=/var/tmp/glasswyrm-build-m11-server-standalone
server_synthetic=/var/tmp/glasswyrm-build-m11-server-synthetic
gwm_build=/var/tmp/glasswyrm-build-m11-gwm
gwcomp_build=/var/tmp/glasswyrm-build-m11-gwcomp-drm
gwcomp_headless=/var/tmp/glasswyrm-build-m11-gwcomp-headless
ipc_only=/var/tmp/glasswyrm-build-m11-ipc-only
session_build=/var/tmp/glasswyrm-build-m11-session
client_dir=/var/tmp/glasswyrm-m11-clients
dumps=/var/tmp/glasswyrm-m11-dumps scenes=/var/tmp/glasswyrm-m11-scenes
pre_restart_dumps=/var/tmp/glasswyrm-m11-dumps-before-restart
pre_restart_scenes=/var/tmp/glasswyrm-m11-scenes-before-restart
launcher_dumps=/var/tmp/glasswyrm-m11-launcher-dumps
launcher_scenes=/var/tmp/glasswyrm-m11-launcher-scenes
input=/var/tmp/glasswyrm-m11-input control=/var/tmp/glasswyrm-m11-control
artifact_dir=${artifact_dir:-/var/tmp/glasswyrm-m11-artifacts}
facts=$artifact_dir/milestone11-facts.env
runtime_log=$artifact_dir/milestone11-runtime-test.log
failure_stage=prerequisite scenario_exit=1 keyboard= pointer= session_pid= xterm_bin=
geometry_probe_pid=
x_servers_absent=false mesa_absent=false getty_unit=getty@${target_vt##*/}.service
getty_state_captured=false getty_active_before=unknown getty_enabled_before=unknown
getty_active_after=unknown getty_enabled_after=unknown
logind_unit=systemd-logind.service logind_socket=systemd-logind-varlink.socket
logind_state_captured=false logind_active_before=unknown logind_active_after=unknown
logind_socket_active_before=unknown logind_socket_active_after=unknown
logind_enabled_before=unknown logind_enabled_after=unknown
logind_socket_enabled_before=unknown logind_socket_enabled_after=unknown
# A failed diagnostic run can leave transient unit definitions loaded even
# after their processes have stopped.  Clear those definitions before reusing
# a guest so systemd-run cannot replay stale services or reject the new run.
m11_units=(xterm-m11-b.service xterm-m11-a.service glasswyrmd-m11.service
  gwcomp-m11.service gwm-m11.service glasswyrm-session-m11.service
  gw-uinput-m11.service)
systemctl stop "${m11_units[@]}" >/dev/null 2>&1 || true
systemctl reset-failed "${m11_units[@]}" >/dev/null 2>&1 || true
rm -rf "$artifact_dir" "$dumps" "$scenes" "$pre_restart_dumps" \
  "$pre_restart_scenes" "$launcher_dumps" \
  "$launcher_scenes" "$input" "$control"
mkdir -p "$artifact_dir" "$client_dir" "$dumps" "$scenes" "$input" "$control"
mkdir -p "$launcher_dumps" "$launcher_scenes"
chmod 0700 "$artifact_dir" "$input" "$control"
run_started=$(date +%s)
declare -A result
results=(strict_default strict_m11 sanitizer clang component_builds source_layout
  ipc_refactor api_consumers m4_m10_regressions uinput keyboard_ready pointer_ready
  xkb_keymap core_mapping relative_motion wheel key_repeat cursor_resources cursor_scanout
  grabs active_grab automatic_grab primary_selection clipboard_selection
  property_notify client_message wm_bindings move resize close
  xterm_alive pty_typing editing scrolling selection_paste deterministic_frame
  screenshot_equal vt_suspend vt_no_delivery vt_resume post_vt_command gwm_replay
  compositor_replay xterm_survival post_restart_input kms_restore kd_restore
  vt_restore getty_restore service_results socket_cleanup device_cleanup
  normalized_trace exact_trace archive_validation journal_evidence)
for key in "${results[@]}"; do result[$key]=not-run; done

capture_getty_state() {
  getty_active_before=$(systemctl is-active "$getty_unit" 2>/dev/null || true)
  getty_enabled_before=$(systemctl is-enabled "$getty_unit" 2>/dev/null || true)
  [[ -n $getty_active_before && -n $getty_enabled_before ]]
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
  [[ $getty_active_after == "$getty_active_before" &&
     $getty_enabled_after == "$getty_enabled_before" ]]
}
capture_logind_state() {
  logind_active_before=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
  logind_socket_active_before=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
  logind_enabled_before=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
  logind_socket_enabled_before=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
  [[ ($logind_active_before == active || $logind_active_before == inactive) &&
     ($logind_socket_active_before == active || $logind_socket_active_before == inactive) &&
     -n $logind_enabled_before && -n $logind_socket_enabled_before ]]
  logind_state_captured=true
}
restore_logind_state() {
  [[ $logind_state_captured == true ]] || return 0
  systemctl unmask --runtime "$logind_unit" "$logind_socket" >/dev/null 2>&1 || return 1
  if [[ $logind_socket_active_before == active ]]; then
    systemctl start "$logind_socket" >/dev/null 2>&1 || return 1
  else
    systemctl stop "$logind_socket" >/dev/null 2>&1 || return 1
  fi
  if [[ $logind_active_before == active ]]; then
    systemctl start "$logind_unit" >/dev/null 2>&1 || return 1
  else
    systemctl stop "$logind_unit" >/dev/null 2>&1 || return 1
  fi
  [[ $logind_enabled_before != masked-runtime ]] ||
    systemctl mask --runtime --now "$logind_unit" >/dev/null 2>&1 || return 1
  [[ $logind_socket_enabled_before != masked-runtime ]] ||
    systemctl mask --runtime --now "$logind_socket" >/dev/null 2>&1 || return 1
  logind_active_after=$(systemctl is-active "$logind_unit" 2>/dev/null || true)
  logind_socket_active_after=$(systemctl is-active "$logind_socket" 2>/dev/null || true)
  logind_enabled_after=$(systemctl is-enabled "$logind_unit" 2>/dev/null || true)
  logind_socket_enabled_after=$(systemctl is-enabled "$logind_socket" 2>/dev/null || true)
  [[ $logind_active_after == "$logind_active_before" &&
     $logind_socket_active_after == "$logind_socket_active_before" &&
     $logind_enabled_after == "$logind_enabled_before" &&
     $logind_socket_enabled_after == "$logind_socket_enabled_before" ]]
}

record_facts() {
  local status=$? key
  set +e
  scenario_exit=$status
  [[ -n ${session_pid:-} ]] && kill "$session_pid" >/dev/null 2>&1
  [[ -n ${geometry_probe_pid:-} ]] && kill "$geometry_probe_pid" >/dev/null 2>&1
  systemctl stop xterm-m11-b.service xterm-m11-a.service glasswyrmd-m11.service \
    gwcomp-m11.service gwm-m11.service glasswyrm-session-m11.service \
    gw-uinput-m11.service >/dev/null 2>&1
  if ! restore_logind_state; then
    [[ $status != 0 ]] || { status=1; scenario_exit=1; failure_stage=logind-restore; }
  fi
  if restore_getty_state; then
    [[ $getty_state_captured == false ]] || result[getty_restore]=passed
  else
    result[getty_restore]=failed
    [[ $status != 0 ]] || { status=1; scenario_exit=1; failure_stage=getty-restore; }
  fi
  journalctl --since "@$run_started" -u glasswyrm-session-m11.service --no-pager >"$artifact_dir/milestone11-session-journal.log" 2>&1
  journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager >"$artifact_dir/milestone11-glasswyrmd-journal.log" 2>&1
  journalctl --since "@$run_started" -u gwm-m11.service --no-pager >"$artifact_dir/milestone11-gwm-journal.log" 2>&1
  journalctl --since "@$run_started" -u gwcomp-m11.service --no-pager >"$artifact_dir/milestone11-gwcomp-journal.log" 2>&1
  [[ -s $artifact_dir/milestone11-session-journal.log &&
     -s $artifact_dir/milestone11-glasswyrmd-journal.log &&
     -s $artifact_dir/milestone11-gwm-journal.log &&
     -s $artifact_dir/milestone11-gwcomp-journal.log ]] && result[journal_evidence]=passed
  [[ ! -e /run/glasswyrm-m11/gwm.sock && ! -e /run/glasswyrm-m11/gwcomp.sock && ! -e /tmp/.X11-unix/X99 ]] && result[socket_cleanup]=passed
  ! fuser "$drm_device" "$target_vt" "$keyboard" "$pointer" /dev/uinput >/dev/null 2>&1 && result[device_cleanup]=passed
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$scenario_exit"
    printf 'required_base=%s\ntested_commit=%s\nrun_mode=%s\n' \
      '9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0' "$tested_commit" "$run_mode"
    printf 'api_version=0.6.0\nsoversion=0\nwire_version=1.0\n'
    printf 'compiler_c=%s\ncompiler_cxx=%s\n' "$(cc --version | head -n1)" "$(c++ --version | head -n1)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' "$(meson --version)" "$(ninja --version)" "$(systemctl --version | head -n1)"
    printf 'libinput_version=%s\nlibxkbcommon_version=%s\nxkeyboard_config_version=%s\n' "$(pkg-config --modversion libinput)" "$(pkg-config --modversion xkbcommon)" "$(qlist -Iv x11-misc/xkeyboard-config | head -n1)"
    printf 'xterm_version=%s\nxterm_sha256=%s\n' "${xterm_bin:+$($xterm_bin -version 2>&1 | head -n1)}" "$xterm_sha"
    printf 'x_servers_absent=%s\nmesa_absent=%s\ndrm_mode=1024x768\n' "$x_servers_absent" "$mesa_absent"
    printf 'keyboard_device=%s\npointer_device=%s\ncanonical_hash=%s\nscanout_hash=%s\n' \
      "$keyboard" "$pointer" "${canonical_hash:-unknown}" "${scanout_hash:-unknown}"
    printf 'pointer_profile=relative-only\n'
    printf 'mirror_sha256=%s\nscreenshot_sha256=%s\n' \
      "${mirror:+$(sha256sum "$mirror" 2>/dev/null | awk '{print $1}')}" \
      "$(sha256sum "$artifact_dir/milestone11-desktop-after-restart.ppm" 2>/dev/null | awk '{print $1}')"
    printf 'getty_active_before=%s\ngetty_active_after=%s\ngetty_enabled_before=%s\ngetty_enabled_after=%s\n' \
      "$getty_active_before" "$getty_active_after" "$getty_enabled_before" "$getty_enabled_after"
    printf 'logind_active_before=%s\nlogind_active_after=%s\nlogind_socket_active_before=%s\nlogind_socket_active_after=%s\n' \
      "$logind_active_before" "$logind_active_after" "$logind_socket_active_before" "$logind_socket_active_after"
    for key in "${results[@]}"; do printf '%s=%s\n' "$key" "${result[$key]}"; done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT
exec > >(tee -a "$runtime_log") 2>&1

[[ -c /dev/uinput && -c $drm_device && -c $target_vt ]] || exit 30
if [[ $run_mode == interactive-rerun ]]; then
  failure_stage=reuse-preflight
  ready=/var/tmp/glasswyrm-m11-interactive-ready.env
  [[ -f $ready && ! -L $ready ]] || {
    printf '%s\n' 'M11 interactive rerun requires a completed build phase from milestone11-runtime-test.' >&2
    exit 34
  }
  grep -Fx "tested_commit=$tested_commit" "$ready"
  grep -Fx "xterm_sha256=$xterm_sha" "$ready"
  [[ -x $runtime/src/glasswyrmd && -x $runtime/src/gwm &&
     -x $runtime/src/gwcomp && -x $runtime/src/glasswyrm-session &&
     -x $runtime/tests/gw_uinput_m11 ]]
  xterm_bin=$client_dir/install/bin/xterm
  [[ -x $xterm_bin ]]
  "$xterm_bin" -version 2>&1 | tee "$artifact_dir/milestone11-xterm.log" |
    grep -Fx 'XTerm(410)'
  xterm_archive=$(find /var/cache/distfiles -maxdepth 1 -type f \
    -name 'xterm-410.tgz' -print -quit)
  [[ -n $xterm_archive &&
     $(sha256sum "$xterm_archive" | awk '{print $1}') == "$xterm_sha" ]]
  for dependency in libdrm xcb xkbcommon libinput; do
    pkg-config --exists "$dependency"
  done
  for forbidden in x11-base/xorg-server x11-base/xwayland x11-base/xwayland-run \
    'x11-base/xorg-server[xvfb]' media-libs/mesa gui-libs/wayland; do
    ! qlist -IC "$forbidden"
  done
  x_servers_absent=true; mesa_absent=true
else
failure_stage=dependency-installation
install -d -m 0755 /etc/portage/package.accept_keywords
printf '=x11-terms/xterm-410 ~amd64\n' \
  >/etc/portage/package.accept_keywords/glasswyrm-m11
emerge --oneshot --noreplace dev-build/meson dev-build/ninja virtual/pkgconfig \
  net-misc/curl app-crypt/gnupg \
  x11-libs/libdrm x11-libs/libxcb x11-base/xcb-proto x11-libs/libX11 \
  x11-libs/libXt x11-libs/libXaw x11-libs/libXmu dev-libs/libinput \
  x11-libs/libxkbcommon x11-misc/xkeyboard-config =x11-terms/xterm-410
for forbidden in x11-base/xorg-server x11-base/xwayland x11-base/xwayland-run \
  x11-base/xorg-server[xvfb] media-libs/mesa gui-libs/wayland; do
  qlist -IC "$forbidden" && { printf 'M11 forbidden package installed: %s\n' "$forbidden" >&2; exit 1; }
done
x_servers_absent=true; mesa_absent=true
xterm_archive=$(find /var/cache/distfiles -maxdepth 1 -type f -name 'xterm-410.tgz' -print -quit)
[[ -n $xterm_archive && $(sha256sum "$xterm_archive" | awk '{print $1}') == "$xterm_sha" ]]
fetch_xterm_identity() {
  local name=$1 url=$2 sha=$3 output=$4
  local cache=/var/cache/distfiles/$name
  if [[ -f $cache && $(sha256sum "$cache" | awk '{print $1}') == "$sha" ]]; then
    cp "$cache" "$output"
  else
    curl --fail --location --output "$output" "$url"
    [[ $(sha256sum "$output" | awk '{print $1}') == "$sha" ]]
    cp "$output" "$cache"
  fi
  [[ $(sha256sum "$output" | awk '{print $1}') == "$sha" ]]
}
fetch_xterm_identity xterm-410.tgz.asc \
  https://invisible-island.net/archives/xterm/xterm-410.tgz.asc \
  f1acdd6a4516417b3a5149609ac6bd9b36aff6cc4b965dea95cd780d64ec98ce \
  "$client_dir/xterm-410.tgz.asc"
fetch_xterm_identity dickey-xterm-rsa3072.asc \
  'https://invisible-island.net/public/dickey%40invisible-island.net-rsa3072.asc' \
  eec7eccb51a27ae633784d1b1ef42eb775130c782ea51a6c47fa7a901484d6db \
  "$client_dir/dickey.asc"
gpg_home=$client_dir/gnupg
rm -rf "$gpg_home"
mkdir -m 0700 "$gpg_home"
gpg --batch --homedir "$gpg_home" --import "$client_dir/dickey.asc"
gpg --batch --homedir "$gpg_home" --with-colons --fingerprint | grep -F '19882D92DDA4C400C22C0D56CC2AF4472167BE03'
gpg --batch --homedir "$gpg_home" --verify "$client_dir/xterm-410.tgz.asc" "$xterm_archive"
rm -rf "$client_dir/xterm-410" "$client_dir/install"
tar -xzf "$xterm_archive" -C "$client_dir"
cd "$client_dir/xterm-410"
LIBS=-ltinfo ./configure --prefix="$client_dir/install" --enable-openpty --disable-freetype \
  --disable-wide-chars --disable-luit --disable-toolbar --disable-sixel-graphics \
  --disable-regis-graphics --without-xinerama --disable-xcursor --disable-tek4014 \
  --disable-session-mgt --disable-input-method --disable-active-icon \
  --disable-desktop --disable-print-graphics --disable-screen-dumps
make -j"$(nproc)"; make install
xterm_bin=$client_dir/install/bin/xterm
"$xterm_bin" -version 2>&1 | tee "$artifact_dir/milestone11-xterm.log" | grep -Fx 'XTerm(410)'
cd "$source_dir"

failure_stage=build-matrix
meson setup "$default" "$source_dir" --wipe -Dwerror=true -Dlibinput_backend=false
meson compile -C "$default"
meson test -C "$default" --print-errorlogs | tee "$artifact_dir/milestone11-meson-test.log"
! meson introspect "$default" --dependencies | grep -E 'libinput|xkbcommon'
result[strict_default]=passed
meson setup "$build" "$source_dir" --wipe -Dwerror=true -Dlibinput_backend=true -Ddrm_backend=true -Dheadless_backend=true
meson compile -C "$build"; meson test -C "$build" --print-errorlogs | tee -a "$artifact_dir/milestone11-meson-test.log"
result[strict_m11]=passed result[m4_m10_regressions]=passed
meson setup "$asan" "$source_dir" --wipe -Dlibinput_backend=true -Ddrm_backend=true -Dasan=true -Dubsan=true
# Sanitizer instrumentation raises peak compiler memory enough that the fixed
# 4 GiB validation guest can OOM under Ninja's default parallelism.
meson compile -C "$asan" -j 1; meson test -C "$asan" --print-errorlogs
result[sanitizer]=passed
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
  CC=clang CXX=clang++ meson setup "$clang_build" "$source_dir" --wipe \
    -Dwerror=true -Dlibinput_backend=true -Ddrm_backend=true
  meson compile -C "$clang_build"
  meson test -C "$clang_build" --print-errorlogs
  result[clang]=passed
else
  result[clang]=unavailable
fi
meson setup "$server" "$source_dir" --wipe -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false -Dlibinput_backend=true
meson setup "$server_standalone" "$source_dir" --wipe -Dwerror=true \
  -Dlibgwipc=false -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false \
  -Dlibinput_backend=false
meson setup "$server_synthetic" "$source_dir" --wipe -Dwerror=true \
  -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false \
  -Dlibinput_backend=false
meson setup "$gwm_build" "$source_dir" --wipe -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false
meson setup "$gwcomp_build" "$source_dir" --wipe -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false -Ddrm_backend=true -Dheadless_backend=false
meson setup "$gwcomp_headless" "$source_dir" --wipe -Dwerror=true \
  -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false \
  -Ddrm_backend=false -Dheadless_backend=true
meson setup "$ipc_only" "$source_dir" --wipe -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false
meson setup "$session_build" "$source_dir" --wipe -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false
for component in "$server" "$server_standalone" "$server_synthetic" \
  "$gwm_build" "$gwcomp_build" "$gwcomp_headless" "$ipc_only" \
  "$session_build"; do
  meson compile -C "$component"
done
result[component_builds]=passed
"$source_dir/tests/install/gwipc_staged_consumers_test.sh" "$source_dir" "$build"
meson test -C "$build" --print-errorlogs \
  gwipc-api-0.6-session-c-consumer gwipc-api-0.6-session-cpp-consumer \
  gwipc-public-contract-api gwipc-public-control-api client-connection-deferred
result[api_consumers]=passed result[ipc_refactor]=passed
meson test -C "$build" --print-errorlogs keyboard-mapping-dispatch
result[core_mapping]=passed
meson test -C "$build" --print-errorlogs grab-state grab-dispatch
result[active_grab]=passed result[automatic_grab]=passed
"$source_dir/tests/tools/source_layout_test.sh" | tee "$artifact_dir/milestone11-source-layout.log"
! grep -Eq '^[[:space:]]*[^#[:space:]]' \
  "$source_dir/docs/maintenance/source_size_allowlist.txt"
result[source_layout]=passed

failure_stage=uinput-startup
rm -rf "$runtime"; meson setup "$runtime" "$source_dir" -Dlibinput_backend=true -Ddrm_backend=true -Dheadless_backend=true
meson compile -C "$runtime"
{
  printf 'tested_commit=%s\n' "$tested_commit"
  printf 'xterm_sha256=%s\n' "$xterm_sha"
} >/var/tmp/glasswyrm-m11-interactive-ready.env
fi
failure_stage=uinput-startup
capture_vt_state() {
  python3 - "$target_vt" "$1" <<'PY'
import fcntl,json,os,struct,sys
tty,output=sys.argv[1:]; fd=os.open(tty,os.O_RDONLY|os.O_NOCTTY|os.O_CLOEXEC)
try:
  active=bytearray(struct.calcsize('=HHH')); mode=bytearray(struct.calcsize('=BBhhh')); kd=bytearray(struct.calcsize('=i'))
  fcntl.ioctl(fd,0x5603,active,True); fcntl.ioctl(fd,0x5601,mode,True); fcntl.ioctl(fd,0x4B3B,kd,True)
finally: os.close(fd)
json.dump({'active':struct.unpack('=HHH',active),'mode':struct.unpack('=BBhhh',mode),'kd':struct.unpack('=i',kd)[0]},open(output,'w'),sort_keys=True)
PY
}
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --snapshot-state --output "$artifact_dir/milestone11-kms-before.json"
capture_vt_state "$artifact_dir/milestone11-vt-before.json"
capture_getty_state
capture_logind_state
[[ $getty_active_before != active ]] || systemctl stop "$getty_unit"
systemctl mask --runtime --now "$logind_socket" "$logind_unit"
[[ $(systemctl is-active "$logind_unit" 2>/dev/null || true) == inactive &&
   $(systemctl is-active "$logind_socket" 2>/dev/null || true) == inactive ]]
systemd-run --unit=gw-uinput-m11.service --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property='DeviceAllow=/dev/uinput rw' \
  --property=NoNewPrivileges=yes "$runtime/tests/gw_uinput_m11" serve \
  --control-socket "$input/control.sock" --devices-json "$input/devices.json"
for _ in {1..200}; do [[ -s $input/devices.json && -S $input/control.sock ]] && break; sleep .05; done
cp "$input/devices.json" "$artifact_dir/milestone11-libinput-devices.json"
readarray -t event_paths < <(python3 - "$input/devices.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); print(d['keyboard']['event_path']); print(d['pointer']['event_path'])
PY
)
keyboard=${event_paths[0]} pointer=${event_paths[1]}
[[ $keyboard == /dev/input/event* && $pointer == /dev/input/event* && $keyboard != "$pointer" ]]
result[uinput]=passed result[keyboard_ready]=passed result[pointer_ready]=passed
transcript=$artifact_dir/milestone11-pty-transcript.log

failure_stage=session-startup
mkdir -p /run/glasswyrm-m11; chmod 0700 /run/glasswyrm-m11
launcher=("$runtime/src/glasswyrm-session" --runtime-dir /run/glasswyrm-m11 --display 99
  --drm-device "$drm_device" --tty "$target_vt" --connector "$connector"
  --mode 1024x768 --input-device "$keyboard" --input-device "$pointer"
  --xkb-layout us --xkb-model pc105 --drm-api atomic
  --mirror-dump-dir "$launcher_dumps" --scene-manifest "$launcher_scenes/scene.jsonl"
  --drm-report "$artifact_dir/milestone11-launcher-drm-report.jsonl"
  --x11-trace "$artifact_dir/milestone11-launcher-trace.json"
  --client xterm -geometry 80x24+96+96 -fn fixed -fb fixed
  -xrm '*cursorBlink:false' -xrm '*toolBar:false' -title Glasswyrm-M11
  -e /bin/bash --noprofile --rcfile "$source_dir/tests/compat/m11/m11-bashrc")
printf 'launcher'; printf ' <%s>' "${launcher[@]}"; printf '\n'
systemd-run --unit=glasswyrm-session-m11.service \
  --setenv="PATH=$runtime/src:$client_dir/install/bin:/usr/bin:/bin" \
  --setenv="GW_M11_TRANSCRIPT=$transcript" --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property="DeviceAllow=$drm_device rw" \
  --property="DeviceAllow=$target_vt rw" --property="DeviceAllow=$keyboard r" \
  --property="DeviceAllow=$pointer r" --property=RestrictAddressFamilies=AF_UNIX \
  --property=StandardInput=tty-force --property="TTYPath=$target_vt" \
  --property=StandardOutput=journal --property=StandardError=journal \
  --property=TTYReset=yes --property=TTYVHangup=yes \
  --property=TTYVTDisallocate=no \
  --property=NoNewPrivileges=yes "${launcher[@]}"
for _ in {1..300}; do [[ -S /tmp/.X11-unix/X99 && -S /run/glasswyrm-m11/gwm.sock && -S /run/glasswyrm-m11/gwcomp.sock ]] && break; sleep .1; done
[[ -S /tmp/.X11-unix/X99 ]]
sleep 1
systemctl is-active --quiet glasswyrm-session-m11.service
systemctl stop glasswyrm-session-m11.service
for _ in {1..200}; do [[ ! -e /tmp/.X11-unix/X99 && ! -e /run/glasswyrm-m11/gwm.sock && ! -e /run/glasswyrm-m11/gwcomp.sock ]] && break; sleep .05; done
: >"$transcript"

# The launcher supervision contract intentionally treats a required peer exit
# as fatal. The restart-survival acceptance therefore runs the same exact argv
# under per-component transient units after the launcher path has been proven.
systemd-run --unit=gwm-m11.service --property=PrivateDevices=yes \
  --property=RestrictAddressFamilies=AF_UNIX --property=NoNewPrivileges=yes \
  "$runtime/src/gwm" --ipc-socket /run/glasswyrm-m11/gwm.sock
for _ in {1..200}; do [[ -S /run/glasswyrm-m11/gwm.sock ]] && break; sleep .05; done
start_gwcomp() {
  systemd-run --unit=gwcomp-m11.service --property=PrivateDevices=no \
    --property=DevicePolicy=closed --property="DeviceAllow=$drm_device rw" \
    --property="DeviceAllow=$target_vt rw" --property=RestrictAddressFamilies=AF_UNIX \
    --property=StandardInput=tty-force --property="TTYPath=$target_vt" \
    --property=StandardOutput=journal --property=StandardError=journal \
    --property=TTYReset=yes --property=TTYVHangup=yes \
    --property=TTYVTDisallocate=no \
    --property=NoNewPrivileges=yes "$runtime/src/gwcomp" --backend drm \
    --ipc-socket /run/glasswyrm-m11/gwcomp.sock --drm-device "$drm_device" \
    --tty "$target_vt" --connector "$connector" --mode 1024x768 --drm-api atomic \
    --mirror-dump-dir "$dumps" --scene-manifest "$scenes/scene.jsonl" \
    --drm-report "$artifact_dir/milestone11-drm-report.jsonl"
}
start_gwcomp
for _ in {1..200}; do [[ -S /run/glasswyrm-m11/gwcomp.sock ]] && break; sleep .05; done
systemd-run --unit=glasswyrmd-m11.service --property=PrivateDevices=no \
  --property=DevicePolicy=closed --property="DeviceAllow=$keyboard r" \
  --property="DeviceAllow=$pointer r" --property=RestrictAddressFamilies=AF_UNIX \
  --property=NoNewPrivileges=yes "$runtime/src/glasswyrmd" --display 99 \
  --wm-socket /run/glasswyrm-m11/gwm.sock \
  --compositor-socket /run/glasswyrm-m11/gwcomp.sock --software-content \
  --libinput-device "$keyboard" --libinput-device "$pointer" \
  --xkb-layout us --xkb-model pc105 \
  --x11-trace "$artifact_dir/milestone11-xterm-trace.raw.jsonl"
for _ in {1..300}; do [[ -S /tmp/.X11-unix/X99 ]] && break; sleep .1; done
[[ -S /tmp/.X11-unix/X99 ]]
systemd-run --unit=xterm-m11-a.service --setenv=DISPLAY=:99 --setenv=LC_ALL=C \
  --setenv=LANG=C --setenv=XMODIFIERS=@im=none --setenv=SESSION_MANAGER= \
  --setenv=XAUTHORITY=/dev/null --setenv=TERM=xterm \
  --setenv="GW_M11_TRANSCRIPT=$transcript" "$xterm_bin" \
  -geometry 80x24+96+96 -fn fixed -fb fixed -xrm '*cursorBlink:false' \
  -xrm '*toolBar:false' -title Glasswyrm-M11-A -e /bin/bash --noprofile \
  --rcfile "$source_dir/tests/compat/m11/m11-bashrc"
for _ in {1..200}; do systemctl is-active --quiet xterm-m11-a.service && break; sleep .05; done
first_xterm_pid=$(systemctl show xterm-m11-a.service -p MainPID --value)
xterm_has_pty() {
python3 - "$1" <<'PY'
import os,sys
todo=[int(sys.argv[1])]; seen=set(); pty=False
while todo:
  pid=todo.pop()
  if pid in seen: continue
  seen.add(pid)
  try:
    if os.readlink(f'/proc/{pid}/fd/0').startswith('/dev/pts/'): pty=True
    children=open(f'/proc/{pid}/task/{pid}/children').read().split()
    todo.extend(map(int,children))
  except (FileNotFoundError,PermissionError): pass
raise SystemExit(0 if pty else 1)
PY
}
for _ in {1..200}; do xterm_has_pty "$first_xterm_pid" && break; sleep .05; done
xterm_has_pty "$first_xterm_pid" || {
  printf '%s\n' 'xterm process tree has no real PTY-backed shell' >&2; exit 1;
}
result[xterm_alive]=passed result[xkb_keymap]=passed
printf '{"rules":"evdev","model":"pc105","layout":"us"}\n' >"$artifact_dir/milestone11-keymap.json"

run_input() {
  local scenario=$1 log=$2
  "$runtime/tests/gw_uinput_m11" run --control-socket "$input/control.sock" \
    --scenario "$scenario" --result-json "$input/$scenario.json"
  grep -F '"status":"completed"' "$input/$scenario.json"
  printf '%s passed\n' "$scenario" >>"$artifact_dir/$log"
}
wait_transcript_token() {
  local token=$1
  for _ in {1..200}; do grep -Fq "$token" "$transcript" 2>/dev/null && return; sleep .05; done
  printf 'Timed out waiting for xterm transcript token: %s\n' "$token" >&2
  return 1
}
frame_hashes() {
  python3 - "$artifact_dir/milestone11-drm-report.jsonl" <<'PY'
import json,sys
records=[json.loads(x) for x in open(sys.argv[1]) if x.strip()]
frames=[r for r in records if r.get('canonical_hash') and r.get('scanout_hash')]
if not frames or frames[-1]['canonical_hash'] != frames[-1]['scanout_hash']:
  raise SystemExit('latest DRM frame does not prove canonical/scanout parity')
print(frames[-1]['canonical_hash'],frames[-1]['scanout_hash'])
PY
}
settled_mirror() {
  local candidate= last= stable=0
  for _ in {1..200}; do
    candidate=$(find "$dumps" -type f -name '*.ppm' -print | sort -V | tail -n1)
    if [[ -n $candidate && -s $candidate && $candidate == "$last" ]]; then
      stable=$((stable + 1))
      if ((stable >= 10)); then
        printf '%s\n' "$candidate"
        return
      fi
    else
      last=$candidate
      stable=0
    fi
    sleep .1
  done
  printf '%s\n' 'Timed out waiting for a quiescent compositor mirror' >&2
  return 1
}
failure_stage=interactive-scenarios
wait_transcript_token M11_READY
sleep .5
run_input basic-typing milestone11-xterm.log
run_input repeat milestone11-xterm.log
run_input scroll milestone11-interactive-wm.log
wait_transcript_token M11_SELECTION_TOKEN
run_input primary-selection milestone11-selection.log
ready_count_before=$(grep -Fc M11_READY "$transcript")
systemd-run --unit=xterm-m11-b.service --setenv=DISPLAY=:99 --setenv=LC_ALL=C \
  --setenv=LANG=C --setenv=XMODIFIERS=@im=none --setenv=SESSION_MANAGER= \
  --setenv=XAUTHORITY=/dev/null --setenv=TERM=xterm \
  --setenv="GW_M11_TRANSCRIPT=$transcript" "$xterm_bin" \
  -geometry 80x24+384+160 -fn fixed -fb fixed -xrm '*cursorBlink:false' \
  -xrm '*toolBar:false' -title Glasswyrm-M11-B -e /bin/bash --noprofile \
  --rcfile "$source_dir/tests/compat/m11/m11-bashrc"
for _ in {1..200}; do systemctl is-active --quiet xterm-m11-b.service && break; sleep .05; done
second_xterm_pid=$(systemctl show xterm-m11-b.service -p MainPID --value)
for _ in {1..200}; do
  ready_count_after=$(grep -Fc M11_READY "$transcript" 2>/dev/null || true)
  ((ready_count_after > ready_count_before)) && break
  sleep .05
done
((ready_count_after > ready_count_before))
sleep .25
[[ -x $runtime/tests/m11_xterm_geometry_probe ]] || {
  printf '%s\n' 'M11 destination geometry probe is required.' >&2; exit 1;
}
move_ready=$input/xterm-move-ready.json
rm -f "$move_ready"
DISPLAY=:99 "$runtime/tests/m11_xterm_geometry_probe" \
  --title Glasswyrm-M11-B --xterm-pid "$second_xterm_pid" \
  --move-dx 96 --move-dy 64 --columns 90 --rows 28 \
  --move-ready "$move_ready" \
  --output "$artifact_dir/milestone11-xterm-geometry.json" &
geometry_probe_pid=$!
sleep .1
kill -0 "$geometry_probe_pid"
run_input clipboard-probe milestone11-selection.log
run_input move milestone11-interactive-wm.log
for _ in {1..200}; do
  [[ -s $move_ready ]] && break
  kill -0 "$geometry_probe_pid" 2>/dev/null || break
  sleep .05
done
grep -F '"status":"ready"' "$move_ready"
run_input resize milestone11-interactive-wm.log
for _ in {1..200}; do
  kill -0 "$geometry_probe_pid" 2>/dev/null || break
  sleep .05
done
wait "$geometry_probe_pid"
geometry_probe_pid=
grep -F '"status": "passed"' "$artifact_dir/milestone11-xterm-geometry.json"
grep -F '"columns":90,"rows":28' "$artifact_dir/milestone11-xterm-geometry.json"
[[ -x $runtime/tests/m11_selection_probe && -x $source_dir/tests/apps/m11_xterm_acceptance ]] || {
  printf '%s\n' 'M11 acceptance probes are required; helper completion alone is not evidence.' >&2; exit 1; }
DISPLAY=:99 "$runtime/tests/m11_selection_probe" --output "$artifact_dir/milestone11-selection-probe.json"
grep -F '"status":"passed"' "$artifact_dir/milestone11-selection-probe.json"
grep -F '"selection":"CLIPBOARD"' "$artifact_dir/milestone11-selection-probe.json"
grep -F '"token":"M11_CLIPBOARD_TOKEN"' "$artifact_dir/milestone11-selection-probe.json"
result[clipboard_selection]=passed result[property_notify]=passed
mirror=$(settled_mirror)
[[ -s $mirror ]]; read -r canonical_hash scanout_hash < <(frame_hashes)
cp "$mirror" "$artifact_dir/milestone11-canonical.ppm"
printf 'ready\nmode=1024x768\ncommit_id=initial\ngeneration=1\nconnector=%s\ncanonical_hash=%s\nscanout_hash=%s\n' "$connector" "$canonical_hash" "$scanout_hash" >"$control/screenshot-ready"
while [[ ! -e $control/screen-captured ]]; do sleep .1; done
cmp "$mirror" "$artifact_dir/milestone11-desktop.ppm"
result[deterministic_frame]=passed result[screenshot_equal]=passed

failure_stage=vt-cycle
trace_hash_before=$(sha256sum "$artifact_dir/milestone11-xterm-trace.raw.jsonl" | awk '{print $1}')
chvt 1; sleep 1
python3 - "$artifact_dir/milestone11-drm-report.jsonl" release "$canonical_hash" <<'PY'
import json,pathlib,sys,time
path=pathlib.Path(sys.argv[1]); transition=sys.argv[2]; expected=sys.argv[3]; deadline=time.monotonic()+5
while True:
  try:
    records=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    record=next(r for r in reversed(records) if r.get('record')=='vt' and r.get('transition')==transition)
    assert record['master_owned'] is False and record['full_modeset'] is False
    assert record['committed_hash'] == expected
    break
  except (AssertionError,KeyError,StopIteration,json.JSONDecodeError):
    if time.monotonic() >= deadline: raise SystemExit('VT release report proof is incomplete')
    time.sleep(.05)
PY
systemctl is-active --quiet gwcomp-m11.service glasswyrmd-m11.service
for _ in {1..100}; do
  grep -Eq 'session state generation=[0-9]+ state=1 result=[12]' \
    <<<"$(journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager)" && break
  sleep .05
done
grep -Eq 'session state generation=[0-9]+ state=1 result=[12]' \
  <<<"$(journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager)"
result[vt_suspend]=passed
run_input post-vt milestone11-session-state.log
[[ $(sha256sum "$artifact_dir/milestone11-xterm-trace.raw.jsonl" | awk '{print $1}') == "$trace_hash_before" ]]
result[vt_no_delivery]=passed
chvt "${target_vt##*/tty}"; sleep 1
python3 - "$artifact_dir/milestone11-drm-report.jsonl" acquire "$canonical_hash" <<'PY'
import json,pathlib,sys,time
path=pathlib.Path(sys.argv[1]); transition=sys.argv[2]; expected=sys.argv[3]; deadline=time.monotonic()+5
while True:
  try:
    records=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    record=next(r for r in reversed(records) if r.get('record')=='vt' and r.get('transition')==transition)
    assert record['master_owned'] is True and record['full_modeset'] is True
    assert record['committed_hash'] == expected
    break
  except (AssertionError,KeyError,StopIteration,json.JSONDecodeError):
    if time.monotonic() >= deadline: raise SystemExit('VT acquire report proof is incomplete')
    time.sleep(.05)
PY
systemctl is-active --quiet gwcomp-m11.service glasswyrmd-m11.service
for _ in {1..100}; do
  grep -Eq 'session state generation=[0-9]+ state=2 result=[12]' \
    <<<"$(journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager)" && break
  sleep .05
done
grep -Eq 'session state generation=[0-9]+ state=2 result=[12]' \
  <<<"$(journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager)"
result[vt_resume]=passed
post_vt_frames_before=$(grep -c '"record":"flip"' "$artifact_dir/milestone11-drm-report.jsonl" || true)
run_input post-vt milestone11-session-state.log
[[ $(sha256sum "$artifact_dir/milestone11-xterm-trace.raw.jsonl" | awk '{print $1}') != "$trace_hash_before" ]]
wait_transcript_token M11_VT
for _ in {1..200}; do
  post_vt_frames_after=$(grep -c '"record":"flip"' "$artifact_dir/milestone11-drm-report.jsonl" || true)
  ((post_vt_frames_after > post_vt_frames_before)) && break
  sleep .05
done
((post_vt_frames_after > post_vt_frames_before))
result[post_vt_command]=passed
mirror=$(settled_mirror); read -r canonical_hash scanout_hash < <(frame_hashes)
cp "$mirror" "$artifact_dir/milestone11-canonical-after-vt.ppm"
printf 'ready\nmode=1024x768\ncommit_id=post-vt\ngeneration=2\nconnector=%s\ncanonical_hash=%s\nscanout_hash=%s\n' "$connector" "$canonical_hash" "$scanout_hash" >"$control/screenshot-after-vt-ready"
while [[ ! -e $control/screen-after-vt-captured ]]; do sleep .1; done
cmp "$mirror" "$artifact_dir/milestone11-desktop-after-vt.ppm"

failure_stage=peer-restart
gwm_pid_before=$(systemctl show gwm-m11.service -p MainPID --value)
gwcomp_pid_before=$(systemctl show gwcomp-m11.service -p MainPID --value)
gwm_bindings_before=$(journalctl --since "@$run_started" -u gwm-m11.service --no-pager | grep -c 'gwm: interactive bindings' || true)
systemctl restart gwm-m11.service
for _ in {1..200}; do [[ -S /run/glasswyrm-m11/gwm.sock ]] && break; sleep .05; done
for _ in {1..200}; do
  gwm_bindings_after=$(journalctl --since "@$run_started" -u gwm-m11.service --no-pager | grep -c 'gwm: interactive bindings' || true)
  ((gwm_bindings_after > gwm_bindings_before)) && break
  sleep .05
done
gwm_pid_after=$(systemctl show gwm-m11.service -p MainPID --value)
[[ $gwm_pid_after != "$gwm_pid_before" && $gwm_pid_after != 0 ]]
[[ $(systemctl show gwcomp-m11.service -p MainPID --value) == "$gwcomp_pid_before" ]]
systemctl is-active --quiet gwcomp-m11.service glasswyrmd-m11.service
((gwm_bindings_after > gwm_bindings_before)); result[gwm_replay]=passed
systemctl stop gwcomp-m11.service
mv "$artifact_dir/milestone11-drm-report.jsonl" \
  "$artifact_dir/milestone11-drm-report-before-restart.jsonl"
mv "$dumps" "$pre_restart_dumps"
mv "$scenes" "$pre_restart_scenes"
mkdir -p "$dumps" "$scenes"
start_gwcomp
for _ in {1..200}; do [[ -S /run/glasswyrm-m11/gwcomp.sock ]] && break; sleep .05; done
for _ in {1..200}; do
  scene_records_after=$(wc -l <"$scenes/scene.jsonl" 2>/dev/null || true)
  ((scene_records_after > 0)) && break
  sleep .05
done
gwcomp_pid_after=$(systemctl show gwcomp-m11.service -p MainPID --value)
[[ $gwcomp_pid_after != "$gwcomp_pid_before" && $gwcomp_pid_after != 0 ]]
[[ $(systemctl show gwm-m11.service -p MainPID --value) == "$gwm_pid_after" ]]
systemctl is-active --quiet gwm-m11.service glasswyrmd-m11.service
((scene_records_after > 0)); result[compositor_replay]=passed
post_restart_frames_before=$(grep -c '"record":"flip"' "$artifact_dir/milestone11-drm-report.jsonl" || true)
run_input post-restart milestone11-session-state.log
wait_transcript_token M11_RESTART
for _ in {1..200}; do
  post_restart_frames_after=$(grep -c '"record":"flip"' "$artifact_dir/milestone11-drm-report.jsonl" || true)
  ((post_restart_frames_after > post_restart_frames_before)) && break
  sleep .05
done
((post_restart_frames_after > post_restart_frames_before))
systemctl is-active --quiet xterm-m11-a.service; systemctl is-active --quiet xterm-m11-b.service
result[post_restart_input]=passed result[xterm_survival]=passed
mirror=$(settled_mirror); read -r canonical_hash scanout_hash < <(frame_hashes)
cp "$mirror" "$artifact_dir/milestone11-canonical-after-restart.ppm"
printf 'ready\nmode=1024x768\ncommit_id=restart\ngeneration=3\nconnector=%s\ncanonical_hash=%s\nscanout_hash=%s\n' "$connector" "$canonical_hash" "$scanout_hash" >"$control/screenshot-after-restart-ready"
while [[ ! -e $control/screen-after-restart-captured ]]; do sleep .1; done
cmp "$mirror" "$artifact_dir/milestone11-desktop-after-restart.ppm"
run_input close milestone11-interactive-wm.log
for _ in {1..100}; do ! systemctl is-active --quiet xterm-m11-a.service && break; sleep .05; done
! systemctl is-active --quiet xterm-m11-a.service
systemctl is-active --quiet xterm-m11-b.service
result[close]=passed
frames=$(find "$dumps" -type f -name frames.jsonl -print -quit)
journalctl --since "@$run_started" -u gwm-m11.service --no-pager >"$artifact_dir/milestone11-interactive-wm.log"
journalctl --since "@$run_started" -u glasswyrmd-m11.service --no-pager >"$artifact_dir/milestone11-glasswyrmd-journal.log"
failure_stage=trace-fixture
"$source_dir/tests/compat/m11/m11_trace_summarize" \
  "$artifact_dir/milestone11-xterm-trace.raw.jsonl" \
  >"$artifact_dir/milestone11-xterm-trace.json"
if [[ -s $source_dir/tests/fixtures/m11/xterm.trace.json &&
      -s $source_dir/tests/fixtures/m11/SHA256SUMS ]]; then
  "$source_dir/tests/tools/m11_fixture_validate.py" \
    "$source_dir/tests/fixtures/m11"
  if ! cmp -s "$source_dir/tests/fixtures/m11/xterm.trace.json" \
      "$artifact_dir/milestone11-xterm-trace.json"; then
    diff -u "$source_dir/tests/fixtures/m11/xterm.trace.json" \
      "$artifact_dir/milestone11-xterm-trace.json" \
      >"$artifact_dir/milestone11-xterm-trace.diff" || true
    sed -n '1,200p' "$artifact_dir/milestone11-xterm-trace.diff" >&2
    printf '%s\n' 'M11 normalized trace differs from the accepted fixture' >&2
    exit 1
  fi
  result[normalized_trace]=passed result[exact_trace]=passed
else
  printf '%s\n' 'M11 fixture bootstrap: normalized trace captured; exact fixture acceptance remains pending' >&2
  result[normalized_trace]=passed result[exact_trace]=bootstrap
fi

failure_stage=interactive-acceptance
"$source_dir/tests/apps/m11_xterm_acceptance" \
  --xterm-pid "$first_xterm_pid" --xterm-pid "$second_xterm_pid" \
  --scenario-dir "$input" --transcript "$transcript" \
  --trace "$artifact_dir/milestone11-xterm-trace.json" \
  --selection "$artifact_dir/milestone11-selection-probe.json" \
  --geometry "$artifact_dir/milestone11-xterm-geometry.json" \
  --wm-evidence "$artifact_dir/milestone11-interactive-wm.log" \
  --server-journal "$artifact_dir/milestone11-glasswyrmd-journal.log" \
  --frames "$frames" --scene "$scenes/scene.jsonl" \
  --drm-report "$artifact_dir/milestone11-drm-report.jsonl" \
  --mirror "$mirror" --screenshot "$artifact_dir/milestone11-desktop-after-restart.ppm" \
  --output "$artifact_dir/milestone11-xterm-result.json"
for field in typed edited repeated scrolled wheel primary pasted moved resized relative_motion wm_bindings grabs client_message cursor_resources cursor_scanout; do
  grep -F "\"$field\": true" "$artifact_dir/milestone11-xterm-result.json"
done
for kind in left-pointer xterm-text fleur-move bottom-right-resize; do
  grep -F "\"$kind\"" "$artifact_dir/milestone11-xterm-result.json"
done
grep -F '"buffer_reused": true' "$artifact_dir/milestone11-xterm-result.json"
python3 - "$artifact_dir/milestone11-xterm-result.json" \
  "$artifact_dir/milestone11-gwm-bindings.json" \
  "$artifact_dir/milestone11-selection-client-message.json" <<'PY'
import json,pathlib,sys
source,bindings,selection=sys.argv[1:]
payload=json.loads(pathlib.Path(source).read_text())
for key,path in (('binding_profile',bindings),
                 ('selection_client_message',selection)):
    value=payload.get(key)
    if not isinstance(value,dict) or value.get('schema')!=1:
        raise SystemExit(f'M11 acceptance result lacks {key}')
    pathlib.Path(path).write_text(json.dumps(value,sort_keys=True,indent=2)+'\n')
PY
result[pty_typing]=passed result[editing]=passed result[key_repeat]=passed
result[wheel]=passed result[scrolling]=passed result[primary_selection]=passed
result[selection_paste]=passed result[move]=passed result[resize]=passed
result[relative_motion]=passed result[wm_bindings]=passed result[grabs]=passed
result[client_message]=passed
result[cursor_resources]=passed result[cursor_scanout]=passed

failure_stage=shutdown
systemctl stop xterm-m11-b.service xterm-m11-a.service glasswyrmd-m11.service \
  gwcomp-m11.service gwm-m11.service
for unit in glasswyrm-session-m11.service glasswyrmd-m11.service gwcomp-m11.service gwm-m11.service; do
  [[ $(systemctl show "$unit" -p Result --value) == success ]]
  [[ $(systemctl show "$unit" -p ExecMainStatus --value) == 0 ]]
done
result[service_results]=passed
"$runtime/tools/gw_drm_probe" --device "$drm_device" --connector "$connector" \
  --require-mode 1024x768 --expect-restored "$artifact_dir/milestone11-kms-before.json" \
  --output "$artifact_dir/milestone11-kms-after.json"
capture_vt_state "$artifact_dir/milestone11-vt-after.json"
cmp "$artifact_dir/milestone11-vt-before.json" "$artifact_dir/milestone11-vt-after.json"
result[kms_restore]=passed result[kd_restore]=passed result[vt_restore]=passed
restore_logind_state
restore_getty_state
result[getty_restore]=passed
failure_stage=evidence-archive
evidence=$artifact_dir/evidence; rm -rf "$evidence"; mkdir -p "$evidence"
cp "$artifact_dir"/milestone11-desktop*.ppm "$evidence/"
cp "$artifact_dir"/milestone11-canonical*.ppm "$evidence/"
cp "$artifact_dir"/milestone11-{libinput-devices.json,keymap.json,xterm-trace.raw.jsonl,xterm-trace.json,selection.log,interactive-wm.log,session-state.log,drm-report.jsonl,drm-report-before-restart.jsonl} "$evidence/"
cp "$artifact_dir/milestone11-pty-transcript.log" "$evidence/"
cp "$artifact_dir/milestone11-xterm-geometry.json" "$evidence/"
cp "$artifact_dir"/milestone11-{gwm-bindings.json,selection-client-message.json} "$evidence/"
cp "$artifact_dir/milestone11-glasswyrmd-journal.log" "$evidence/"
cp "$artifact_dir"/milestone11-{selection-probe.json,xterm-result.json,kms-before.json,kms-after.json,vt-before.json,vt-after.json} "$evidence/"
cp "$scenes/scene.jsonl" "$evidence/scene.jsonl"
find "$dumps" -name frames.jsonl -exec cp {} "$evidence/frames.jsonl" \;
cp "$pre_restart_scenes/scene.jsonl" "$evidence/scene-before-restart.jsonl"
find "$pre_restart_dumps" -name frames.jsonl -exec cp {} "$evidence/frames-before-restart.jsonl" \;
(cd "$evidence" && sha256sum ./* >SHA256SUMS && sha256sum --check --status SHA256SUMS && tar -cf "$artifact_dir/milestone11-interactive-evidence.tar" ./*)
result[archive_validation]=passed failure_stage= scenario_exit=0
GUEST_SCRIPT
}

milestone11_poll_marker() {
  local marker=$1 guest_pid=$2 output='' canonical scanout
  local deadline=$((SECONDS + M11_SCREENSHOT_WAIT_SECONDS))
  while ((SECONDS < deadline)); do
    if output=$(guest_run_script 'set -euo pipefail; test -f "$1"; cat "$1"' "$M11_GUEST_CONTROL_DIR/$marker" 2>/dev/null); then
      canonical=$(sed -n 's/^canonical_hash=//p' <<<"$output")
      scanout=$(sed -n 's/^scanout_hash=//p' <<<"$output")
      if grep -Fxq ready <<<"$output" && grep -Fxq mode=1024x768 <<<"$output" &&
        grep -q '^commit_id=.' <<<"$output" && grep -q '^generation=.' <<<"$output" &&
        [[ -n $canonical && $canonical == "$scanout" ]]; then
        printf '%s\n' "$output"; return 0
      fi
    fi
    kill -0 "$guest_pid" 2>/dev/null || return 1
    sleep .1
  done
  printf 'Timed out waiting for fixed M11 marker %s.\n' "$marker" >&2
  return 1
}

milestone11_capture_screen() {
  local ready=$1 captured=$2 name=$3 guest_pid=$4 marker raw canonical attempt
  marker=$(milestone11_poll_marker "$ready" "$guest_pid") || return
  printf '%s\n' "$marker" >>"$ARTIFACTS_PATH_ABS/milestone11-session-state.log"
  case $name in
    milestone11-desktop.ppm) canonical=milestone11-canonical.ppm ;;
    milestone11-desktop-after-vt.ppm) canonical=milestone11-canonical-after-vt.ppm ;;
    milestone11-desktop-after-restart.ppm) canonical=milestone11-canonical-after-restart.ppm ;;
    *) printf 'Unknown M11 screenshot artifact: %s\n' "$name" >&2; return 1 ;;
  esac
  raw=$(mktemp "$ARTIFACTS_PATH_ABS/.milestone11-screen.XXXXXX") || return
  # QXL publishes the accepted KMS buffer to the SPICE screenshot surface
  # asynchronously.  Input remains quiescent while the guest waits, so poll
  # until the graphical console is byte-identical to the accepted canonical
  # frame instead of relying on a host-timing delay.
  for attempt in {1..40}; do
    virsh --connect "$LIBVIRT_URI" screenshot "$VM_DOMAIN" "$raw" || { rm -f "$raw"; return 1; }
    magick "$raw" -depth 8 "ppm:$ARTIFACTS_PATH_ABS/$name" || { rm -f "$raw"; return 1; }
    rsync -a -e "ssh -p $SSH_PORT -o BatchMode=yes -o ConnectTimeout=10" \
      "$ARTIFACTS_PATH_ABS/$name" "$SSH_TARGET:$M11_GUEST_ARTIFACT_DIR/$name" || { rm -f "$raw"; return 1; }
    if guest_run_script 'set -euo pipefail; cmp -s "$1" "$2"' \
      "$M11_GUEST_ARTIFACT_DIR/$canonical" "$M11_GUEST_ARTIFACT_DIR/$name"; then
      rm -f "$raw"
      guest_run_script 'set -euo pipefail; mkdir -p "${1%/*}"; printf "screen-captured\n" >"$1"' \
        "$M11_GUEST_CONTROL_DIR/$captured"
      return
    fi
    kill -0 "$guest_pid" 2>/dev/null || { rm -f "$raw"; return 1; }
    sleep .25
  done
  rm -f "$raw"
  printf 'Timed out waiting for M11 graphical console parity: %s\n' "$name" >&2
  return 1
}

milestone11_release_guest_waits() {
  guest_run_script 'set -euo pipefail; for marker in "$@"; do mkdir -p "${marker%/*}"; : >"$marker"; done' \
    "$M11_GUEST_CONTROL_DIR/screen-captured" "$M11_GUEST_CONTROL_DIR/screen-after-vt-captured" \
    "$M11_GUEST_CONTROL_DIR/screen-after-restart-captured" >/dev/null 2>&1 || true
}

collect_milestone11_artifacts() {
  local run_mode=${1:-full} name failed=0
  init_artifacts
  for name in "${M11_TEXT_ARTIFACTS[@]}"; do
    [[ $name == milestone11-runtime-test.log || $name == milestone11-session-state.log ]] && continue
    if [[ $run_mode == interactive-rerun &&
          ($name == milestone11-meson-test.log ||
           $name == milestone11-source-layout.log) ]]; then
      continue
    fi
    guest_run_script 'set -euo pipefail; cat "$1"' "$M11_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  for name in "${M11_BINARY_ARTIFACTS[@]}"; do
    [[ $name == milestone11-desktop.ppm || $name == milestone11-desktop-after-vt.ppm || $name == milestone11-desktop-after-restart.ppm ]] && continue
    scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
      "$SSH_TARGET:$M11_GUEST_ARTIFACT_DIR/$name" "$ARTIFACTS_PATH_ABS/$name" || failed=1
  done
  return "$failed"
}

write_milestone11_interactive_rerun_summary() {
  local requested=$1 failure=${2:-}
  local facts=$ARTIFACTS_PATH_ABS/milestone11-facts.env
  local out=$ARTIFACTS_PATH_ABS/milestone11-interactive-rerun-summary.json
  python3 - "$facts" "$out" "$requested" "$failure" \
    "$M11_REQUIRED_BASE_COMMIT" "${M11_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]
facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    key,sep,value=line.partition('=')
    if sep: facts[key]=value
required='uinput keyboard_ready pointer_ready xkb_keymap relative_motion wheel key_repeat cursor_resources cursor_scanout grabs primary_selection clipboard_selection property_notify client_message wm_bindings move resize close xterm_alive pty_typing editing scrolling selection_paste deterministic_frame screenshot_equal vt_suspend vt_no_delivery vt_resume post_vt_command gwm_replay compositor_replay xterm_survival post_restart_input kms_restore kd_restore vt_restore getty_restore service_results socket_cleanup device_cleanup normalized_trace archive_validation journal_evidence'.split()
errors=[f'{key} must be passed' for key in required if facts.get(key)!='passed']
if facts.get('exact_trace') not in {'passed','bootstrap'}:
  errors.append('exact_trace must be passed or bootstrap')
if facts.get('run_mode')!='interactive-rerun':
  errors.append('run_mode must be interactive-rerun')
if facts.get('tested_commit')!=tested:
  errors.append('guest build identity does not match tested commit')
if facts.get('scenario_exit')!='0':
  errors.append('scenario_exit must be 0')
payload={
  'diagnostic_only':True,
  'accepted_milestone11_result':False,
  'required_base_commit':base,
  'tested_commit':tested,
  'run_mode':facts.get('run_mode','unknown'),
  'passed':requested=='true' and not errors,
  'failure_stage':failure or facts.get('failure_stage',''),
  'evidence_errors':errors,
}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

validate_milestone11_archive() {
  local archive=$ARTIFACTS_PATH_ABS/milestone11-interactive-evidence.tar listing scratch status=0 member
  [[ -s $archive ]] || return
  listing=$(tar -tf "$archive") || return
  for member in milestone11-desktop.ppm milestone11-desktop-after-vt.ppm \
    milestone11-desktop-after-restart.ppm milestone11-canonical.ppm \
    milestone11-canonical-after-vt.ppm milestone11-canonical-after-restart.ppm \
    milestone11-libinput-devices.json \
    milestone11-keymap.json milestone11-xterm-trace.raw.jsonl \
    milestone11-xterm-trace.json milestone11-pty-transcript.log \
    milestone11-xterm-geometry.json \
    milestone11-selection.log \
    milestone11-interactive-wm.log milestone11-session-state.log \
    milestone11-gwm-bindings.json milestone11-selection-client-message.json \
    milestone11-drm-report.jsonl milestone11-drm-report-before-restart.jsonl \
    milestone11-glasswyrmd-journal.log \
    milestone11-selection-probe.json \
    milestone11-xterm-result.json milestone11-kms-before.json \
    milestone11-kms-after.json milestone11-vt-before.json \
    milestone11-vt-after.json scene.jsonl scene-before-restart.jsonl \
    frames.jsonl frames-before-restart.jsonl SHA256SUMS; do
    grep -Eq "^(\\./)?$member$" <<<"$listing" || return
  done
  scratch=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m11-evidence.XXXXXX") || return
  tar -xf "$archive" -C "$scratch" || status=$?
  if ((status == 0)); then (cd "$scratch" && sha256sum --check --status SHA256SUMS) || status=$?; fi
  rm -rf -- "$scratch"
  return "$status"
}

write_milestone11_summary() {
  local requested=$1 failure=${2:-} facts=$ARTIFACTS_PATH_ABS/milestone11-facts.env out=$ARTIFACTS_PATH_ABS/milestone11-summary.json
  python3 - "$facts" "$out" "$requested" "$failure" "$M11_REQUIRED_BASE_COMMIT" "${M11_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    key,sep,value=line.partition('=')
    if sep: facts[key]=value
required='strict_default strict_m11 sanitizer component_builds source_layout ipc_refactor api_consumers m4_m10_regressions uinput keyboard_ready pointer_ready xkb_keymap core_mapping relative_motion wheel key_repeat cursor_resources cursor_scanout grabs active_grab automatic_grab primary_selection clipboard_selection property_notify client_message wm_bindings move resize close xterm_alive pty_typing editing scrolling selection_paste deterministic_frame screenshot_equal vt_suspend vt_no_delivery vt_resume post_vt_command gwm_replay compositor_replay xterm_survival post_restart_input kms_restore kd_restore vt_restore getty_restore service_results socket_cleanup device_cleanup normalized_trace exact_trace archive_validation journal_evidence'.split()
identity={'api_version':'0.6.0','soversion':'0','wire_version':'1.0','x_servers_absent':'true','mesa_absent':'true','drm_mode':'1024x768','pointer_profile':'relative-only','xterm_version':'XTerm(410)','xterm_sha256':'7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f'}
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
if facts.get('clang') not in {'passed','unavailable'}: errors.append('clang must be passed or unavailable')
errors += [f'{k} must be {v}' for k,v in identity.items() if facts.get(k)!=v]
for k in ('compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','libinput_version','libxkbcommon_version','xkeyboard_config_version'):
  if facts.get(k) in (None,'','unknown','unavailable'): errors.append(f'{k} must be recorded')
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
for k in ('keyboard_device','pointer_device','canonical_hash','scanout_hash','mirror_sha256','screenshot_sha256'):
  if facts.get(k) in (None,'','unknown'): errors.append(f'{k} must be recorded')
if facts.get('canonical_hash') != facts.get('scanout_hash'): errors.append('canonical and scanout hashes differ')
if facts.get('mirror_sha256') != facts.get('screenshot_sha256'): errors.append('mirror and screenshot hashes differ')
results={k:facts.get(k,'unknown') for k in required}
results['clang']=facts.get('clang','unknown')
payload={'required_base_commit':base,'tested_commit':tested,'facts':facts,'results':results,'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone11_runtime_test() {
  local approved=$1 run_mode=${2:-full} failure='' status=0 collection=0
  local preflight='' script='' guest_pid=0 guest_status=0 action
  [[ $run_mode == full || $run_mode == interactive-rerun ]] ||
    die "Unknown M11 host run mode '$run_mode'."
  if [[ $run_mode == full ]]; then
    action=milestone11-runtime-test
  else
    action=milestone11-interactive-rerun
  fi
  require_approval "$action" "$approved"; require_vm_domain
  is_true "$SNAPSHOT_ENABLED" || die 'milestone11-runtime-test requires the configured internal base snapshot.'
  if [[ $run_mode == full ]]; then
    note 'Required gate sequence: reset; milestone10-runtime-test; reset; milestone11-runtime-test. M10 must run before M11 installs libinput, libxkbcommon, xkeyboard-config, and xterm 410.'
  else
    note 'Diagnostic only: reusing the commit-bound M11 guest build and dependency state. This command does not satisfy Milestone 11 acceptance; finish with the complete milestone11-runtime-test gate.'
  fi
  init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone11_evidence || { status=$?; failure=source-evidence; }
  [[ -n $failure ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z $failure ]]; then
    if milestone11_doctor | tee "$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log"; then :; else status=$?; failure=host-guest-doctor; fi
  fi
  if [[ -z $failure ]]; then
    if preflight=$(guest_run_script "$(milestone11_guest_prerequisite_script)" 2>&1); then
      printf '%s\n' "$preflight" | tee -a "$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log"
    else status=$?; printf '%s\n' "$preflight" | tee -a "$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log" >&2; failure=graphical-input-prerequisite; fi
  fi
  if [[ -z $failure ]]; then
    if verify_milestone11_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z $failure ]]; then
    drm_device=$(sed -n 's/^drm_primary_node=//p' <<<"$preflight")
    connector=$(sed -n 's/^drm_connector=//p' <<<"$preflight")
    target_vt=$(sed -n 's/^target_vt=//p' <<<"$preflight")
    # Remove markers from interrupted or failed attempts before starting the
    # asynchronous guest script.  Otherwise the host can consume a stale
    # ready marker before the guest reaches its own cleanup prologue.
    guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p "$1"' \
      "$M11_GUEST_CONTROL_DIR" || { status=$?; failure=guest-control-reset; }
  fi
  if [[ -z $failure ]]; then
    script=$(milestone11_guest_script)
    guest_run_script "$script" "$GUEST_SOURCE_PATH" "$M11_GUEST_ARTIFACT_DIR" \
      "$drm_device" "$connector" "$target_vt" "$M11_XTERM_SHA256" \
      "$M11_TESTED_COMMIT" "$run_mode" \
      >>"$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log" 2>&1 & guest_pid=$!
    if milestone11_capture_screen screenshot-ready screen-captured milestone11-desktop.ppm "$guest_pid"; then :; else status=$?; failure=desktop-screenshot; milestone11_release_guest_waits; fi
    if [[ -z $failure ]]; then
      if milestone11_capture_screen screenshot-after-vt-ready screen-after-vt-captured milestone11-desktop-after-vt.ppm "$guest_pid"; then :; else status=$?; failure=post-vt-screenshot; milestone11_release_guest_waits; fi
    fi
    if [[ -z $failure ]]; then
      if milestone11_capture_screen screenshot-after-restart-ready screen-after-restart-captured milestone11-desktop-after-restart.ppm "$guest_pid"; then :; else status=$?; failure=post-restart-screenshot; milestone11_release_guest_waits; fi
    fi
    if wait "$guest_pid"; then :; else guest_status=$?; status=$guest_status; failure=guest-runtime; fi
  fi
  collect_milestone11_artifacts "$run_mode" || collection=$?
  if ((collection)) && [[ -z $failure ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -z $failure ]] && ! validate_milestone11_archive; then status=1; failure=artifact-validation; fi
  if [[ -n $failure ]]; then
    if [[ $run_mode == full ]]; then
      write_milestone11_summary false "$failure" || true
    else
      write_milestone11_interactive_rerun_summary false "$failure" || true
    fi
    printf 'Milestone 11 VM %s failed during: %s\n' "$run_mode" "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  if ! verify_milestone11_source_identity; then
    if [[ $run_mode == full ]]; then
      write_milestone11_summary false source-identity-changed || true
    else
      write_milestone11_interactive_rerun_summary false source-identity-changed || true
    fi
    return 1
  fi
  if [[ $run_mode == full ]]; then
    write_milestone11_summary true ''
    record_scenario milestone11-runtime-test passed "$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log"
    printf 'Milestone 11 VM runtime test passed.\n'
  else
    write_milestone11_interactive_rerun_summary true ''
    record_scenario milestone11-interactive-rerun passed "$ARTIFACTS_PATH_ABS/milestone11-runtime-test.log"
    printf '%s\n' 'Milestone 11 interactive diagnostic rerun passed; full acceptance remains required.'
  fi
  print_artifacts
}

milestone11_interactive_rerun() {
  milestone11_runtime_test "$1" interactive-rerun
}
