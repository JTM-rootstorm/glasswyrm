#!/usr/bin/env bash
# shellcheck disable=SC2016 # Assertions intentionally match literal guest scripts.

set -euo pipefail

repo_root=${1:-}
if [[ -z ${repo_root} ]]; then
  printf 'usage: %s REPOSITORY_ROOT\n' "$0" >&2
  exit 2
fi

gw_vm=${repo_root}/tools/gw-vm
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-gw-vm-test.XXXXXX")

fake_bin=${work_dir}/bin
mkdir -p "$repo_root/artifacts/vm"
artifact_dir=$(mktemp -d "$repo_root/artifacts/vm/cli-test.XXXXXX")
rerun_artifact_dir=$artifact_dir/interactive-rerun
trap 'rm -rf "$work_dir" "$artifact_dir"' EXIT
overlay_dir=${work_dir}/overlay
source_dir=${artifact_dir}/source
guest_source_dir=${work_dir}/guest-source
command_log=${work_dir}/commands.log
config_file=${work_dir}/config.toml
no_domain_config=${work_dir}/no-domain.toml
mkdir -p "$fake_bin" "$artifact_dir" "$overlay_dir" "$source_dir"
touch "$source_dir/meson.build"

fail() {
  printf 'gw-vm CLI test failed: %s\n' "$*" >&2
  exit 1
}

run_success() {
  local output_file=$1
  shift
  if ! "$@" >"$output_file" 2>&1; then
    sed 's/^/  /' "$output_file" >&2
    fail "command unexpectedly failed: $*"
  fi
}

run_failure() {
  local output_file=$1
  shift
  if "$@" >"$output_file" 2>&1; then
    sed 's/^/  /' "$output_file" >&2
    fail "command unexpectedly succeeded: $*"
  fi
}

run_allow_failure() {
  local output_file=$1
  shift
  "$@" >"$output_file" 2>&1 || true
}

assert_contains() {
  local file=$1
  local expected=$2
  grep -F -- "$expected" "$file" >/dev/null || {
    sed 's/^/  /' "$file" >&2
    fail "$file does not contain: $expected"
  }
}

assert_not_contains() {
  local file=$1
  local unexpected=$2
  if grep -F -- "$unexpected" "$file" >/dev/null; then
    sed 's/^/  /' "$file" >&2
    fail "$file unexpectedly contains: $unexpected"
  fi
}

assert_before() {
  local file=$1 first=$2 second=$3 first_line second_line
  first_line=$(awk -v needle="$first" 'index($0, needle) { print NR; exit }' "$file")
  second_line=$(awk -v needle="$second" 'index($0, needle) { print NR; exit }' "$file")
  [[ -n $first_line && -n $second_line && $first_line -lt $second_line ]] ||
    fail "$file does not place '$first' before '$second'"
}

assert_file_glob() {
  local pattern=$1
  compgen -G "$pattern" >/dev/null || fail "no file matched: $pattern"
}

semver_helper=${repo_root}/tools/gw-vm.d/lib/common.sh
for compatible in '0.1.0 0.1.0' '0.2.0 0.1.0' '0.10.0 0.3.0' \
  '1.0.0 0.3.0' '0.3.1 0.3.0'; do
  read -r actual minimum <<<"$compatible"
  bash -c 'source "$1"; semantic_version_at_least "$2" "$3"' _ \
    "$semver_helper" "$actual" "$minimum" ||
    fail "semantic version helper rejected $actual >= $minimum"
done
for incompatible in '0.0.9 0.1.0' '0.2.9 0.3.0' '0.3 0.3.0' \
  'unknown 0.1.0' '0.3.0 0.3'; do
  read -r actual minimum <<<"$incompatible"
  if bash -c 'source "$1"; semantic_version_at_least "$2" "$3"' _ \
    "$semver_helper" "$actual" "$minimum"; then
    fail "semantic version helper accepted $actual >= $minimum"
  fi
done

cat >"$config_file" <<EOF
[libvirt]
uri = "test:///glasswyrm"
domain = "glasswyrm-test"

[snapshot]
name = "pristine"
enabled = true

[guest]
ssh_host = "guest.test"
ssh_user = "root"
ssh_port = 2222
shared_overlay_path = "/mnt/shared/glasswyrm-overlay"
shared_artifacts_path = "/mnt/shared/glasswyrm-artifacts"
shared_source_path = "$guest_source_dir"

[paths]
overlay = "$overlay_dir"
artifacts = "$artifact_dir"
source = "$source_dir"

[portage]
overlay_name = "glasswyrm-test"
repos_conf_path = "/etc/portage/repos.conf/glasswyrm-test.conf"
default_package = "x11-base/glasswyrm"

[safety]
allow_destructive = false
allow_arbitrary_ssh = false
EOF

sed 's/domain = "glasswyrm-test"/domain = ""/' "$config_file" >"$no_domain_config"

cat >"$fake_bin/virsh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'virsh' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ -n ${GW_VM_TEST_FAIL_VIRSH:-} && " $* " == *" $GW_VM_TEST_FAIL_VIRSH "* ]]; then
  printf 'injected virsh failure for %s\n' "$GW_VM_TEST_FAIL_VIRSH" >&2
  exit 41
fi
case " $* " in
  *' domstate '*) printf 'running\n' ;;
  *' dumpxml '*)
    if [[ ${GW_VM_TEST_QXL_LOW:-0} == 1 ]]; then
      printf '<domain><devices><video><model type="qxl" vgamem="16384"/></video><graphics type="spice"/></devices></domain>\n'
    else
      printf '<domain><devices><video><model type="virtio"/></video><graphics type="spice"/></devices></domain>\n'
    fi
    ;;
  *' help screenshot '*) printf 'screenshot DOMAIN [FILE]\n' ;;
  *' screenshot '*)
    destination=${!#}
    python3 - "$destination" <<'PY'
import pathlib,sys
pathlib.Path(sys.argv[1]).write_bytes(b'P6\n1024 768\n255\n' + bytes(1024*768*3))
PY
    ;;
  *' start '*) printf 'Domain started\n' ;;
  *' shutdown '*) printf 'Domain is being shutdown\n' ;;
  *' snapshot-revert '*) printf 'Snapshot reverted\n' ;;
esac
EOF

cat >"$fake_bin/magick" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'magick' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
source=$1
destination=${!#}
cp "$source" "${destination#ppm:}"
EOF

cat >"$fake_bin/ssh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'ssh' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"

script=$(cat)
guest_args=()
after_separator=false
for argument in "$@"; do
  if [[ $after_separator == true ]]; then
    guest_args+=("$argument")
  elif [[ $argument == -- ]]; then
    after_separator=true
  fi
done

if [[ $script == *'action=$1'* ]]; then
  action=${guest_args[0]:-}
  package=${guest_args[1]:-}
  printf 'guest-portage <%s> <%s>\n' "$action" "$package" >>"$GW_VM_TEST_COMMAND_LOG"
  if [[ ${GW_VM_TEST_FAIL_MATCH:-} == "$action" ]]; then
    printf 'injected guest failure for %s\n' "$action" >&2
    exit 42
  fi

  case "$action:$package" in
    pretend:x11-wm/gwm)
      printf '[ebuild  N     ] x11-wm/gwm-0.1.0\n'
      if [[ ${GW_VM_TEST_ABI_REBUILDS:-0} == 1 ]]; then
        printf '[ebuild  rR    ] x11-base/glasswyrmd-0.1.0\n'
        printf '[ebuild  rR    ] x11-base/gwcomp-0.1.0\n'
      fi
      ;;
    pretend:*) printf '[ebuild  N     ] x11-base/glasswyrm-0.1.0\n' ;;
    metadata:*) printf 'Regenerating cache entries...\n' ;;
    unmerge:*) printf 'Unmerging package...\n' ;;
    emerge:*) printf 'Emerging package...\n' ;;
  esac
  exit 0
fi

if [[ $script == *'marker="$destination/.glasswyrm-vm-source"'* ]]; then
  printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
  bash -s -- "${guest_args[@]}" <<<"$script"
  exit $?
fi

if [[ $script == *'M10 prerequisite failed before package installation: no DRM primary node'* && $script != *'build=/var/tmp/glasswyrm-build-m10'* ]]; then
  printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
  if [[ ${GW_VM_TEST_M10_NO_DRM:-0} == 1 ]]; then
    printf '%s\n' 'M10 prerequisite failed before package installation: no DRM primary node (/dev/dri/card*) is exposed by the current guest kernel.' >&2
    printf '%s\n' 'Configure the libvirt video device and its virtual GPU DRM driver in the clean snapshot; the M10 harness will never install or rebuild a kernel.' >&2
    exit 20
  fi
  cat <<'PREFLIGHT'
drm_primary_node=/dev/dri/card0
drm_driver=virtio_gpu
drm_connector=Virtual-1
drm_connectors=Virtual-1
drm_modes=Virtual-1:1024x768
drm_mode=1024x768
target_vt=/dev/tty2
virtual_terminals=/dev/tty1,/dev/tty2
PREFLIGHT
  exit 0
fi

if [[ $script == *'M11 prerequisite failed before package installation: /dev/uinput is unavailable'* && $script != *'build=/var/tmp/glasswyrm-build-m11'* ]]; then
  printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
  if [[ ${GW_VM_TEST_M11_NO_UINPUT:-0} == 1 ]]; then
    printf '%s\n' 'M11 prerequisite failed before package installation: /dev/uinput is unavailable; the base snapshot kernel must provide CONFIG_INPUT_UINPUT.' >&2
    exit 30
  fi
  cat <<'PREFLIGHT'
drm_primary_node=/dev/dri/card0
drm_connector=Virtual-1
drm_mode=1024x768
target_vt=/dev/tty2
uinput_device=/dev/uinput
PREFLIGHT
  exit 0
fi

if [[ $script == *'sed -n "s/^full_build_commit=//p"'* ]]; then
  printf '%s\n' '75c68dc000000000000000000000000000000000'
  exit 0
fi

if [[ $script == *'test -f "$1"; cat "$1"'* ]]; then
  marker=${guest_args[0]:-}
  printf 'ready\ncommit_id=%s\ngeneration=1\ncanonical_hash=0123456789abcdef\nscanout_hash=0123456789abcdef\nmode=1024x768\nconnector=Virtual-1\n' "${marker##*/}"
  exit 0
fi

if [[ $script == *'cat "$1"'* ]]; then
  artifact=${guest_args[0]:-}
  case ${artifact##*/} in
    milestone1-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
x_servers_absent=true
meson_tests=passed
sanitizer=passed
raw_clients=passed
xcb_probe=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_FACTS:-0} == 1 ]]; then
        printf 'x_servers_absent=false\nscenario_exit=1\n'
      fi
      ;;
    milestone1-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_JOURNAL:-0} != 1 ]]; then
        printf 'collected current invocation journal\n'
      fi
      ;;
    milestone1-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone2-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
xcb_proto=x11-base/xcb-proto-1.17.0
x_servers_absent=true
m1_regression_tests=passed
m2_tests=passed
sanitizer=passed
raw_little=passed
raw_big=passed
error_continuation=passed
resource_cleanup=passed
cross_endian_property=passed
xcb_setup=passed
xcb_m2=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M2_FACTS:-0} == 1 ]]; then
        printf 'resource_cleanup=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone2-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M2_JOURNAL:-0} != 1 ]]; then
        printf 'collected current M2 invocation journal\n'
      fi
      ;;
    milestone2-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone3-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
FACTS
      printf 'api_version=%s\n' "${GW_VM_TEST_M3_API_VERSION:-0.1.0}"
      cat <<'FACTS'
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
ipc_only=passed
install_layout=passed
c_consumer=passed
cpp_consumer=passed
handshake=passed
ping_pong=passed
contract_roundtrip=passed
fd_transfer=passed
snapshot=passed
version_rejection=passed
role_rejection=passed
capability_rejection=passed
malformed_isolation=passed
sequence_isolation=passed
limit_isolation=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M3_FACTS:-0} == 1 ]]; then
        printf 'sequence_isolation=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone3-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M3_JOURNAL:-0} != 1 ]]; then
        printf 'collected current M3 invocation journal\n'
      fi
      ;;
    milestone3-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone5-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.11.1
ninja_version=1.13.2
systemd_version=systemd 257
api_version=0.3.0
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
gwm_only=passed
ipc_only=passed
legacy_consumers=passed
policy_consumers=passed
basic=passed
snapshot_order=passed
transient=passed
override_redirect=passed
focus=passed
stacking=passed
fullscreen=passed
maximize_minimize=passed
incremental_update=passed
invalid_context_isolation=passed
invalid_window_isolation=passed
unknown_reference_recovery=passed
cycle_rejection=passed
snapshot_abort=passed
malformed_peer_isolation=passed
reconnect_equality=passed
golden_hash_count=10
policy_archive=passed
systemd_runtime=passed
socket_cleanup=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M5_FACTS:-0} == 1 ]]; then printf 'snapshot_abort=failed\nscenario_exit=1\n'; fi
      if [[ ${GW_VM_TEST_BAD_M5_API:-0} == 1 ]]; then printf 'api_version=0.2.0\n'; fi
      ;;
    milestone5-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M5_JOURNAL:-0} != 1 ]]; then printf 'collected current M5 invocation journal\n'; fi
      ;;
    milestone5-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone6-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.4.0
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
runtime_build=passed
server_standalone=passed
server_ipc=passed
gwm_only=passed
gwcomp_only=passed
ipc_only=passed
api04_consumers=passed
integrated_little_big=passed
no_ppm=passed
scene_archive=passed
restart_probe=passed
xcb_m6_probe=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M6_FACTS:-0} == 1 ]]; then
        printf 'restart_probe=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone6-restart.json)
      printf '{"completed":true,"connection_preserved":true}\n'
      ;;
    milestone6-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone7-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.4.0
soversion=0
wire_version=1.0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
xcb_proto=x11-base/xcb-proto-1.17.0
scene_manifest=absent
x_servers_absent=true
full_tests=passed
sanitizer=passed
runtime_build=passed
server_standalone=passed
server_ipc=passed
gwm_only=passed
gwcomp_only=passed
ipc_only=passed
api04_consumers=passed
m4_pixel_regression=passed
m5_policy_regression=passed
m6_metadata_regression=passed
raw_little=passed
raw_big=passed
image_byte_order=passed
exposure_events=passed
malformed_x11=passed
malformed_gwipc=passed
x11_resources=passed
raster_requests=passed
plane_mask=passed
damage_coalescing=passed
xcb_drawing=passed
final_frame_golden=passed
buffer_release=passed
compositor_restart=passed
gwm_restart=passed
connection_survival=passed
replay_hash=passed
post_restart_hash=passed
post_restart_drawing=passed
rendering_archive=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M7_FACTS:-0} == 1 ]]; then
        printf 'final_frame_golden=failed\nscenario_exit=1\n'
      fi
      if [[ ${GW_VM_TEST_BAD_M7_REPLAY:-0} == 1 ]]; then
        printf 'replay_hash=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone7-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone8-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.5.0
soversion=0
wire_version=1.0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
xcb_proto=x11-base/xcb-proto-1.17.0
scene_manifest=present
x_servers_absent=true
libinput_absent=true
sanitizer=passed
FACTS
      for result in full_tests runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api05_consumers m4_pixel_regression m5_policy_regression m6_metadata_regression m7_drawable_regression input_socket_security raw_little raw_big motion crossing buttons button_motion keyboard modifiers propagation do_not_propagate click_focus focus_events scene_change_crossing two_client_routing xcb_drawing final_frame_golden event_trace_golden malformed_provider_isolation input_reconnect gwm_restart compositor_restart input_connection_survival x11_connection_survival focus_replay pointer_replay replay_hash post_restart_hash post_restart_input no_device_access service_results socket_cleanup rendering_archive archive_validation journal_evidence; do
        printf '%s=passed\n' "$result"
      done
      if [[ ${GW_VM_TEST_BAD_M8_FACTS:-0} == 1 ]]; then
        printf 'event_trace_golden=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone8-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone9-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.5.0
soversion=0
wire_version=1.0
x_servers_absent=true
mesa_absent=true
libdrm_absent=true
libinput_absent=true
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
xeyes_version=1.3.1
xclock_version=1.2.0
FACTS
      for result in full_tests sanitizer runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api05_consumers client_versions xeyes xclock_analog xclock_digital combined normalized_traces exact_frames restart_replay policy_replay post_restart_input m4_pixel_regression m5_policy_regression m6_metadata_no_ppm_regression m7_drawable_regression m8_input_regression service_results socket_cleanup journal_evidence archive_validation; do
        printf '%s=passed\n' "$result"
      done
      if [[ ${GW_VM_TEST_BAD_M9_FACTS:-0} == 1 ]]; then
        printf 'xclock_digital=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone9-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone10-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.5.0
soversion=0
wire_version=1.0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
libdrm_version=2.4.124
x_servers_absent=true
mesa_absent=true
libinput_absent=true
drm_primary_node=/dev/dri/card0
drm_driver=virtio_gpu
drm_connector=Virtual-1
drm_mode=1024x768
drm_crtc=42
drm_primary_plane=43
dumb_buffer=true
atomic_capability=true
drm_api=atomic
atomic_test_only=passed
canonical_hash=0123456789abcdef
scanout_hash=0123456789abcdef
mirror_hash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
screenshot_hash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
getty_unit=getty@tty2.service
getty_active_before=active
getty_enabled_before=enabled
getty_active_after=active
getty_enabled_after=enabled
FACTS
      for result in strict_tests source_layout_audit source_layout_budget refactor_parity sanitizer clang headless_no_libdrm dual_backend drm_only historical_components m4_m9_regressions m9_golden_mirror initial_modeset page_flip delayed_ack delayed_release hash_parity screenshot_equal vt_release vt_acquire remodeset post_vt_repaint post_vt_screenshot_equal kms_restore kd_restore vt_mode_restore active_vt_restore getty_restore device_exclusivity service_hardening service_results socket_cleanup archive_validation journal_evidence; do
        printf '%s=passed\n' "$result"
      done
      if [[ ${GW_VM_TEST_BAD_M10_FACTS:-0} == 1 ]]; then printf 'page_flip=failed\nscenario_exit=1\n'; fi
      if [[ ${GW_VM_TEST_BAD_M10_GETTY:-0} == 1 ]]; then printf 'getty_active_after=inactive\n'; fi
      ;;
    milestone10-drm-probe.json) printf '{"device":"/dev/dri/card0","driver":"virtio_gpu","connector":"Virtual-1","mode":"1024x768"}\n' ;;
    milestone10-drm-report.jsonl) printf '{"record":"selection","api":"atomic"}\n' ;;
    milestone10-kms-*.json|milestone10-vt-*.json) printf '{}\n' ;;
    milestone10-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone11-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.6.0
soversion=0
wire_version=1.0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.11.1
ninja_version=1.13.2
systemd_version=systemd 257
libinput_version=1.31.3
libxkbcommon_version=1.13.1
xkeyboard_config_version=x11-misc/xkeyboard-config-2.47
xterm_version=XTerm(410)
xterm_sha256=7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f
x_servers_absent=true
mesa_absent=true
drm_mode=1024x768
keyboard_device=/dev/input/event11
pointer_device=/dev/input/event12
pointer_profile=relative-only
canonical_hash=0123456789abcdef
scanout_hash=0123456789abcdef
mirror_sha256=aaaaaaaaaaaaaaaa
screenshot_sha256=aaaaaaaaaaaaaaaa
FACTS
      if [[ $artifact == *glasswyrm-m11-rerun-artifacts* ]]; then
        printf '%s\n' \
          'run_mode=interactive-rerun' \
          'tested_commit=86dab3c000000000000000000000000000000000' \
          'full_build_commit=75c68dc000000000000000000000000000000000' \
          'runtime_commit=86dab3c000000000000000000000000000000000'
      else
        printf '%s\n' \
          'run_mode=full' \
          'tested_commit=86dab3c000000000000000000000000000000000' \
          'full_build_commit=86dab3c000000000000000000000000000000000' \
          'runtime_commit=86dab3c000000000000000000000000000000000'
      fi
      for result in strict_default strict_m11 sanitizer clang component_builds source_layout ipc_refactor api_consumers m4_m10_regressions uinput keyboard_ready pointer_ready xkb_keymap core_mapping relative_motion wheel key_repeat cursor_resources cursor_scanout grabs active_grab automatic_grab primary_selection clipboard_selection property_notify client_message wm_bindings move resize close xterm_alive pty_typing editing scrolling selection_paste deterministic_frame screenshot_equal vt_suspend vt_no_delivery vt_resume post_vt_command gwm_replay compositor_replay xterm_survival post_restart_input kms_restore kd_restore vt_restore getty_restore service_results socket_cleanup device_cleanup normalized_trace exact_trace archive_validation journal_evidence; do
        printf '%s=passed\n' "$result"
      done
      if [[ ${GW_VM_TEST_BAD_M11_FACTS:-0} == 1 ]]; then printf 'selection_paste=failed\nscenario_exit=1\n'; fi
      ;;
    milestone11-libinput-devices.json) printf '{"keyboard":{"event_path":"/dev/input/event11"},"pointer":{"event_path":"/dev/input/event12"}}\n' ;;
    milestone11-keymap.json) printf '{"model":"pc105","layout":"us"}\n' ;;
    milestone11-gwm-bindings.json) printf '{"schema":1,"source":"gwm-journal","replay_verified":true}\n' ;;
    milestone11-selection-client-message.json) printf '{"schema":1,"source":"normalized-x11-trace"}\n' ;;
    milestone11-xterm-trace.raw.jsonl) printf '{"direction":"connection","client":1,"outcome":"accepted"}\n' ;;
    milestone11-xterm-trace.json) printf '{"gwm_replay":true,"compositor_replay":true}\n' ;;
    milestone11-xterm-geometry.json) printf '{"schema":1,"status":"passed"}\n' ;;
    milestone11-drm-report*.jsonl) printf '{"state":"active"}\n' ;;
    milestone11-rerun-provenance.env)
      printf '%s\n' 'schema=1' 'kind=milestone11-interactive-rerun' \
        'accepted_milestone11_result=false' ;;
    milestone11-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    *) printf 'unexpected artifact request: %s\n' "$artifact" >&2; exit 44 ;;
  esac
  exit 0
fi

printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
if [[ ${GW_VM_TEST_M10_MISSING_SCENE:-0} == 1 &&
      $script == *'M10 scene manifest is missing or empty:'* ]]; then
  printf '%s\n' 'M10 scene manifest is missing or empty: /var/tmp/glasswyrm-m10-scenes/scene.jsonl' >&2
  exit 1
fi
if [[ -n ${GW_VM_TEST_FAIL_MATCH:-} && $script == *"$GW_VM_TEST_FAIL_MATCH"* ]]; then
  printf 'injected guest failure for %s\n' "$GW_VM_TEST_FAIL_MATCH" >&2
  exit 42
fi

case "$script" in
  *'emerge --info'*) printf 'Portage 3.0 test guest\n' ;;
  *'eselect profile list'*) printf '  [1] default/linux/amd64/23.0 *\n' ;;
  *'qlist -Iv'*) printf 'sys-apps/portage-3.0\nx11-base/glasswyrm-0.1.0\n' ;;
  *'/var/log/emerge.log'*) printf 'test emerge log\n' ;;
  *'/var/log/portage/elog/summary.log'*) printf 'test portage elog\n' ;;
esac
EOF

cat >"$fake_bin/rsync" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'rsync' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ ${GW_VM_TEST_FAIL_RSYNC:-0} == 1 ]]; then
  printf 'injected rsync failure\n' >&2
  exit 43
fi
EOF

cat >"$fake_bin/scp" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'scp' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ "$*" == *milestone5-policies.tar* ]]; then
  destination=${!#}
  scratch=$(mktemp -d)
  printf '{}\n' >"$scratch/basic.json"
  (cd "$scratch" && sha256sum basic.json >SHA256SUMS && tar -cf "$destination" basic.json SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone6-lifecycle.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  printf '{}\n' >"$scratch/scene.jsonl"
  (cd "$scratch" && sha256sum scene.jsonl >SHA256SUMS && tar -cf "$destination" scene.jsonl SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone7-rendering.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  printf 'P6\n1 1\n255\n000' >"$scratch/final.ppm"
  printf '{}\n' >"$scratch/frames.jsonl"
  printf '{}\n' >"$scratch/xcb-result.json"
  printf '{}\n' >"$scratch/result.json"
  (cd "$scratch" && sha256sum final.ppm frames.jsonl xcb-result.json result.json >SHA256SUMS && tar -cf "$destination" final.ppm frames.jsonl xcb-result.json result.json SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone8-input-rendering.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  printf 'P6\n1 1\n255\n000' >"$scratch/final.ppm"
  cp "$scratch/final.ppm" "$scratch/selected-runtime.ppm"
  for name in frames.jsonl scene.jsonl raw-little-events.json raw-big-events.json input-acknowledgements.json xcb-result.json result.json; do printf '{}\n' >"$scratch/$name"; done
  (cd "$scratch" && sha256sum final.ppm selected-runtime.ppm frames.jsonl scene.jsonl raw-little-events.json raw-big-events.json input-acknowledgements.json xcb-result.json result.json >SHA256SUMS && tar -cf "$destination" final.ppm selected-runtime.ppm frames.jsonl scene.jsonl raw-little-events.json raw-big-events.json input-acknowledgements.json xcb-result.json result.json SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone9-acceptance.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  if [[ $destination == *.sha256 ]]; then
    (cd "$(dirname "$destination")" && sha256sum milestone9-acceptance.tar >"$(basename "$destination")")
  else
    mkdir -p "$scratch/glasswyrm-m9-control" "$scratch/glasswyrm-m9-traces" "$scratch/glasswyrm-m9-scenes"
    for name in xeyes xclock-analog xclock-digital; do
      printf '{}\n' >"$scratch/glasswyrm-m9-control/$name.json"
      printf 'frame\n' >"$scratch/glasswyrm-m9-control/$name.frame"
    done
    if [[ ${GW_VM_TEST_BAD_M9_ARCHIVE:-0} == 1 ]]; then
      rm -f "$scratch/glasswyrm-m9-control/xclock-digital.frame"
    fi
    printf '{}\n' >"$scratch/glasswyrm-m9-traces/requests.jsonl"
    printf '{}\n' >"$scratch/glasswyrm-m9-scenes/frames.jsonl"
    (cd "$scratch" && tar -cf "$destination" glasswyrm-m9-control glasswyrm-m9-traces glasswyrm-m9-scenes)
  fi
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone10-drm-evidence.tar* ]]; then
  if [[ ${GW_VM_TEST_M10_MISSING_SCENE:-0} == 1 ]]; then
    printf '%s\n' 'M10 evidence archive is unavailable after the scene-manifest guard failed.' >&2
    exit 1
  fi
  destination=${!#}; scratch=$(mktemp -d)
  printf 'P6\n1 1\n255\n000' >"$scratch/canonical.ppm"
  cp "$scratch/canonical.ppm" "$scratch/milestone10-screen.ppm"
  cp "$scratch/canonical.ppm" "$scratch/milestone10-screen-after-vt.ppm"
  for name in frames.jsonl scene.jsonl milestone10-drm-report.jsonl milestone10-kms-before.json milestone10-kms-active.json milestone10-kms-after.json milestone10-vt-before.json milestone10-vt-active.json milestone10-vt-after.json pinned-client-result.json; do printf '{}\n' >"$scratch/$name"; done
  if [[ ${GW_VM_TEST_BAD_M10_ARCHIVE:-0} == 1 ]]; then rm -f "$scratch/milestone10-kms-after.json"; fi
  (cd "$scratch" && sha256sum ./* >SHA256SUMS && tar -cf "$destination" ./*)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone11-interactive-evidence.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  for name in milestone11-desktop.ppm milestone11-desktop-after-vt.ppm milestone11-desktop-after-restart.ppm milestone11-canonical.ppm milestone11-canonical-after-vt.ppm milestone11-canonical-after-restart.ppm; do
    printf 'P6\n1 1\n255\n000' >"$scratch/$name"
  done
  for name in milestone11-libinput-devices.json milestone11-keymap.json milestone11-xterm-trace.raw.jsonl milestone11-xterm-trace.json milestone11-pty-transcript.log milestone11-xterm-geometry.json milestone11-selection.log milestone11-interactive-wm.log milestone11-gwm-bindings.json milestone11-selection-client-message.json milestone11-session-state.log milestone11-drm-report.jsonl milestone11-drm-report-before-restart.jsonl milestone11-glasswyrmd-journal.log milestone11-selection-probe.json milestone11-xterm-result.json milestone11-kms-before.json milestone11-kms-after.json milestone11-vt-before.json milestone11-vt-after.json scene.jsonl scene-before-restart.jsonl frames.jsonl frames-before-restart.jsonl; do printf '{}\n' >"$scratch/$name"; done
  if [[ $destination == *interactive-rerun* ]]; then
    printf '%s\n' 'schema=1' 'kind=milestone11-interactive-rerun' \
      'accepted_milestone11_result=false' \
      >"$scratch/milestone11-rerun-provenance.env"
  fi
  if [[ ${GW_VM_TEST_BAD_M11_ARCHIVE:-0} == 1 ]]; then rm -f "$scratch/scene.jsonl"; fi
  (cd "$scratch" && sha256sum ./* >SHA256SUMS && tar -cf "$destination" ./*)
  rm -rf "$scratch"
fi
EOF

cat >"$fake_bin/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'git' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
case " $* " in
  *' status --porcelain --untracked-files=all '*)
    if [[ ${GW_VM_TEST_GIT_DIRTY:-0} == 1 ]]; then
      printf ' M tracked-source.cpp\n'
    fi
    ;;
  *' rev-parse HEAD '*) printf '86dab3c000000000000000000000000000000000\n' ;;
  *' merge-base --is-ancestor '*)
    [[ ${GW_VM_TEST_BAD_BASE:-0} != 1 ]]
    ;;
  *) printf 'unexpected fake git command: %s\n' "$*" >&2; exit 45 ;;
esac
EOF

chmod +x "$fake_bin/virsh" "$fake_bin/magick" "$fake_bin/ssh" "$fake_bin/rsync" "$fake_bin/scp" "$fake_bin/git"

export PATH="$fake_bin:$PATH"
export GW_VM_TEST_COMMAND_LOG=$command_log
export GLASSWYRM_VM_CONFIG=$config_file
unset GLASSWYRM_VM_NAME GLASSWYRM_VM_SSH_HOST GLASSWYRM_VM_SSH_USER
unset GLASSWYRM_VM_SSH_PORT GLASSWYRM_VM_LIBVIRT_URI GLASSWYRM_VM_SNAPSHOT
unset GLASSWYRM_VM_OVERLAY_PATH GLASSWYRM_VM_ARTIFACTS_PATH

[[ -x $gw_vm ]] || fail "$gw_vm is missing or not executable"

run_success "$work_dir/help.out" "$gw_vm" help
for command in doctor status reset pretend emerge unmerge narrow-test collect full-packaging-test push-source milestone1-runtime-test milestone2-runtime-test milestone3-runtime-test milestone4-runtime-test milestone5-runtime-test milestone6-runtime-test milestone7-runtime-test milestone10-runtime-test milestone11-runtime-test milestone12-runtime-test milestone11-interactive-rerun; do
  assert_contains "$work_dir/help.out" "$command"
done

run_allow_failure "$work_dir/doctor.out" env GLASSWYRM_VM_CONFIG="$no_domain_config" "$gw_vm" doctor
assert_contains "$work_dir/doctor.out" 'GLASSWYRM_VM_NAME'

run_failure "$work_dir/doctor-qxl-low.out" \
  env GW_VM_TEST_QXL_LOW=1 "$gw_vm" doctor
assert_contains "$work_dir/doctor-qxl-low.out" \
  'QXL requires at least 65536 KiB vgamem for M10 double buffering'

run_failure "$work_dir/status-no-domain.out" env GLASSWYRM_VM_CONFIG="$no_domain_config" "$gw_vm" status
assert_contains "$work_dir/status-no-domain.out" 'No VM domain configured.'
assert_contains "$work_dir/status-no-domain.out" 'GLASSWYRM_VM_NAME'

: >"$command_log"
run_success "$work_dir/status.out" "$gw_vm" status
assert_contains "$work_dir/status.out" 'glasswyrm-test'
assert_contains "$work_dir/status.out" 'test:///glasswyrm'
assert_contains "$work_dir/status.out" 'root@guest.test'
assert_contains "$command_log" 'virsh <--connect> <test:///glasswyrm> <domstate> <glasswyrm-test>'

: >"$command_log"
run_success "$work_dir/status-override.out" env GLASSWYRM_VM_NAME=override-domain "$gw_vm" status
assert_contains "$command_log" '<domstate> <override-domain>'
assert_not_contains "$command_log" '<domstate> <glasswyrm-test>'

: >"$command_log"
run_failure "$work_dir/package-injection.out" "$gw_vm" pretend --help
assert_contains "$work_dir/package-injection.out" 'Invalid package atom'
[[ ! -s $command_log ]] || fail 'invalid package reached a host transport command'

run_failure "$work_dir/scenario-path.out" "$gw_vm" scenario ../pretend-glasswyrm
assert_contains "$work_dir/scenario-path.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-injection.out" "$gw_vm" scenario 'milestone1-runtime-test;touch'
assert_contains "$work_dir/scenario-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m2-injection.out" "$gw_vm" scenario 'milestone2-runtime-test;touch'
assert_contains "$work_dir/scenario-m2-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m3-injection.out" "$gw_vm" scenario 'milestone3-runtime-test;touch'
assert_contains "$work_dir/scenario-m3-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m4-injection.out" "$gw_vm" scenario 'milestone4-runtime-test;touch'
assert_contains "$work_dir/scenario-m4-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m5-injection.out" "$gw_vm" scenario 'milestone5-runtime-test;touch'
assert_contains "$work_dir/scenario-m5-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m6-injection.out" "$gw_vm" scenario 'milestone6-runtime-test;touch'
run_failure "$work_dir/scenario-m7-injection.out" "$gw_vm" scenario 'milestone7-runtime-test;touch'
run_failure "$work_dir/scenario-m11-injection.out" "$gw_vm" scenario 'milestone11-runtime-test;touch'
run_failure "$work_dir/scenario-m12-injection.out" "$gw_vm" scenario 'milestone12-runtime-test;touch'
assert_contains "$work_dir/scenario-m6-injection.out" 'Scenario names are fixed'
assert_contains "$work_dir/scenario-m12-injection.out" 'Scenario names are fixed'

: >"$command_log"
run_failure "$work_dir/reset-gate.out" "$gw_vm" reset
assert_contains "$work_dir/reset-gate.out" '--yes'
assert_not_contains "$command_log" 'snapshot-revert'
run_success "$work_dir/reset.out" "$gw_vm" reset --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'

: >"$command_log"
run_success "$work_dir/pretend.out" "$gw_vm" pretend x11-base/glasswyrm
assert_contains "$command_log" 'guest-portage <pretend> <x11-base/glasswyrm>'
assert_file_glob "$artifact_dir/pretend-*.log"

rm -f "$artifact_dir/pretend-x11-base-glasswyrm.log"
mkdir "$artifact_dir/pretend-x11-base-glasswyrm.log"
run_failure "$work_dir/pretend-log-failure.out" "$gw_vm" pretend x11-base/glasswyrm
rmdir "$artifact_dir/pretend-x11-base-glasswyrm.log"
assert_contains "$work_dir/pretend-log-failure.out" 'VM scenario failed'

: >"$command_log"
run_failure "$work_dir/emerge-gate.out" "$gw_vm" emerge x11-base/glasswyrm
assert_contains "$work_dir/emerge-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
run_success "$work_dir/emerge.out" "$gw_vm" emerge x11-base/glasswyrm --yes
assert_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_file_glob "$artifact_dir/emerge-*.log"

: >"$command_log"
run_failure "$work_dir/unmerge-gate.out" "$gw_vm" unmerge x11-wm/gwm
assert_contains "$work_dir/unmerge-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
run_success "$work_dir/unmerge.out" "$gw_vm" unmerge x11-wm/gwm --yes
assert_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
assert_file_glob "$artifact_dir/unmerge-*.log"

run_success "$work_dir/narrow-pass.out" "$gw_vm" narrow-test gwm
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": true'
assert_contains "$artifact_dir/narrow-test-gwm.json" '"unexpected_rebuilds": []'

run_failure "$work_dir/narrow-fail.out" env GW_VM_TEST_ABI_REBUILDS=1 "$gw_vm" narrow-test gwm
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": false'
assert_contains "$artifact_dir/narrow-test-gwm.json" 'x11-base/glasswyrmd'
assert_contains "$artifact_dir/narrow-test-gwm.json" 'x11-base/gwcomp'

run_success "$work_dir/narrow-override.out" env GW_VM_TEST_ABI_REBUILDS=1 "$gw_vm" narrow-test gwm --allow-abi-rebuild
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": true'

rm -f "$artifact_dir/narrow-test-gwm.log"
mkdir "$artifact_dir/narrow-test-gwm.log"
run_failure "$work_dir/narrow-log-failure.out" "$gw_vm" narrow-test gwm
rmdir "$artifact_dir/narrow-test-gwm.log"

run_success "$work_dir/collect.out" "$gw_vm" collect
for artifact in emerge-info.txt profiles.txt installed-packages.txt emerge-log-tail.txt portage-elog-tail.txt summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "collect did not create $artifact"
done

: >"$command_log"
run_allow_failure "$work_dir/collect-partial.out" env GW_VM_TEST_FAIL_MATCH='emerge --info' "$gw_vm" collect
assert_contains "$command_log" 'emerge --info'
assert_contains "$command_log" 'eselect profile list'
assert_contains "$command_log" 'qlist -Iv'
assert_contains "$artifact_dir/summary.json" '"passed": false'

: >"$command_log"
run_failure "$work_dir/full-gate.out" "$gw_vm" full-packaging-test
assert_contains "$work_dir/full-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <emerge>'
assert_not_contains "$command_log" 'guest-portage <unmerge>'

: >"$command_log"
run_success "$work_dir/full.out" "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'
assert_contains "$command_log" 'rsync'
assert_contains "$command_log" 'guest-portage <metadata> <>'
assert_contains "$command_log" 'guest-portage <pretend> <x11-base/glasswyrm>'
assert_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_contains "$command_log" 'guest-portage <pretend> <x11-wm/gwm>'
assert_contains "$command_log" 'guest-portage <emerge> <x11-wm/gwm>'
assert_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
assert_contains "$command_log" 'emerge --info'
assert_contains "$artifact_dir/summary.json" '"passed": true'

run_success "$work_dir/scenario-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/pretend-glasswyrm.sh"

: >"$command_log"
run_failure "$work_dir/full-failure.out" env GW_VM_TEST_FAIL_MATCH=metadata "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" 'guest-portage <metadata> <>'
assert_contains "$command_log" 'emerge --info'
assert_not_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_contains "$work_dir/full-failure.out" 'Artifacts:'

: >"$command_log"
run_failure "$work_dir/full-reset-failure.out" \
  env GW_VM_TEST_FAIL_VIRSH=snapshot-revert "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'
assert_not_contains "$command_log" 'rsync'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_failure "$work_dir/full-marker-failure.out" \
  env GW_VM_TEST_FAIL_MATCH=.glasswyrm-vm-overlay "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '.glasswyrm-vm-overlay'
assert_not_contains "$command_log" 'rsync'
assert_not_contains "$command_log" 'guest-portage <metadata>'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_failure "$work_dir/full-rsync-failure.out" \
  env GW_VM_TEST_FAIL_RSYNC=1 "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" 'rsync'
assert_not_contains "$command_log" 'guest-portage <metadata>'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_success "$work_dir/push-source.out" "$gw_vm" push-source
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" "<$source_dir/>"
assert_contains "$command_log" '<--filter=- /.git/>'
assert_contains "$command_log" '<--filter=- /Plans/>'
assert_contains "$command_log" '<--filter=- /artifacts/>'
assert_contains "$command_log" '<--filter=- /build-*/>'
assert_contains "$command_log" '<--filter=- /tools/gw-vm.d/config.toml>'

: >"$command_log"
rm -f "$guest_source_dir/.glasswyrm-vm-source"
mkdir -p "$guest_source_dir"
touch "$guest_source_dir/not-owned"
run_failure "$work_dir/push-source-marker-failure.out" "$gw_vm" push-source
assert_contains "$work_dir/push-source-marker-failure.out" 'Refusing to replace unowned source destination'
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_not_contains "$command_log" 'rsync'
rm -rf "$guest_source_dir"

: >"$command_log"
symlink_target=${work_dir}/symlink-target
mkdir -p "$symlink_target"
touch "$symlink_target/must-survive"
ln -s "$symlink_target" "$guest_source_dir"
run_failure "$work_dir/push-source-symlink-failure.out" "$gw_vm" push-source
assert_contains "$work_dir/push-source-symlink-failure.out" 'Refusing symlink source destination'
assert_not_contains "$command_log" 'rsync'
[[ -f $symlink_target/must-survive ]] || fail 'symlink destination target was modified'
rm -f "$guest_source_dir"

: >"$command_log"
run_failure "$work_dir/milestone1-gate.out" "$gw_vm" milestone1-runtime-test
assert_contains "$work_dir/milestone1-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone1-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$work_dir/milestone1-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone1.out" "$gw_vm" milestone1-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'systemd-run --unit="$unit"'
assert_contains "$command_log" 'portageq match / "$1"'
assert_contains "$command_log" '_SYSTEMD_INVOCATION_ID=$unit_invocation_id'
assert_contains "$command_log" 'x11_setup_probe'
assert_contains "$command_log" 'xcb_setup_probe'
for artifact in \
  milestone1-runtime-test.log \
  milestone1-meson-test.log \
  milestone1-xcb-probe.log \
  milestone1-journal.log \
  milestone1-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone1 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone1-summary.json" '"sanitizer": "passed"'
assert_contains "$artifact_dir/milestone1-summary.json" '"xorg_xwayland_absent": true'

run_failure "$work_dir/milestone1-bad-facts.out" \
  env GW_VM_TEST_BAD_FACTS=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone1-summary.json" '"evidence_errors"'

run_failure "$work_dir/milestone1-empty-journal.out" \
  env GW_VM_TEST_EMPTY_JOURNAL=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$artifact_dir/milestone1-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone1-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone1-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone1-error.out" \
  env GW_VM_TEST_FAIL_MATCH='systemd-run --unit="$unit"' "$gw_vm" milestone1-runtime-test --yes
assert_contains "$work_dir/milestone1-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone1-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone2-gate.out" "$gw_vm" milestone2-runtime-test
assert_contains "$work_dir/milestone2-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone2-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-bad-base.out" \
  'HEAD is not based on required Milestone 2 commit 4e219a8093c2b79857efc046c3bf0948cc7704f8'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone2-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone2.out" "$gw_vm" milestone2-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'unit=glasswyrmd-m2.service'
assert_contains "$command_log" 'build_dir=/var/tmp/glasswyrm-build-m2'
assert_contains "$command_log" 'sanitizer_build_dir=/var/tmp/glasswyrm-build-m2-asan'
assert_contains "$command_log" 'artifact_dir=$2'
assert_contains "$command_log" 'portageq match / "$1"'
assert_contains "$command_log" '_SYSTEMD_INVOCATION_ID=$unit_invocation_id'
assert_contains "$command_log" '"$setup_probe" --display :99 --byte-order little'
assert_contains "$command_log" '"$setup_probe" --display :99 --byte-order big'
assert_contains "$command_log" '"$m2_probe" --display :99 --byte-order little --basic'
assert_contains "$command_log" '"$m2_probe" --display :99 --byte-order big --basic'
assert_contains "$command_log" '"$m2_probe" --display :99 --errors'
assert_contains "$command_log" '"$m2_probe" --display :99 --cleanup'
assert_contains "$command_log" '"$m2_probe" --display :99 --cross-endian'
assert_contains "$command_log" 'DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_setup_probe"'
assert_contains "$command_log" 'DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_m2_probe"'
for artifact in \
  milestone2-runtime-test.log \
  milestone2-meson-test.log \
  milestone2-raw-probe.log \
  milestone2-xcb-probe.log \
  milestone2-journal.log \
  milestone2-facts.env \
  milestone2-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone2 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone2-summary.json" \
  '"required_base_commit": "4e219a8093c2b79857efc046c3bf0948cc7704f8"'
assert_contains "$artifact_dir/milestone2-summary.json" '"m1_regression_tests": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"cross_endian_property": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"xcb_m2": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"xorg_xwayland_absent": true'

run_failure "$work_dir/milestone2-bad-facts.out" \
  env GW_VM_TEST_BAD_M2_FACTS=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone2-summary.json" '"evidence_errors"'
assert_contains "$artifact_dir/milestone2-summary.json" 'resource_cleanup must be passed'

run_failure "$work_dir/milestone2-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M2_JOURNAL=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$artifact_dir/milestone2-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone2-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone2-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone2-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=glasswyrmd-m2.service' "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone2-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone3-gate.out" "$gw_vm" milestone3-runtime-test
assert_contains "$work_dir/milestone3-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone3-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-bad-base.out" \
  'HEAD is not based on required Milestone 3 commit d6816484f0293b8b47edf0f13e5da691014e3e7c'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone3-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone3.out" "$gw_vm" milestone3-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'build_dir=/var/tmp/glasswyrm-build-m3'
assert_contains "$command_log" 'sanitizer_build_dir=/var/tmp/glasswyrm-build-m3-asan'
assert_contains "$command_log" 'ipc_build_dir=/var/tmp/glasswyrm-build-m3-ipc-only'
assert_contains "$command_log" 'install_root=/var/tmp/glasswyrm-m3-install'
assert_contains "$command_log" 'unit=gwipc-m3.service'
assert_contains "$command_log" '--property=RuntimeDirectory=glasswyrm-m3'
assert_contains "$command_log" '-Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false'
assert_contains "$command_log" 'DESTDIR="$install_root" meson install'
for mode in roundtrip ping-pong contract-roundtrip fd-transfer snapshot \
  incompatible-version wrong-role missing-capability malformed-envelope \
  sequence-violation limit; do
  assert_contains "$command_log" "run_client $mode"
done
for artifact in \
  milestone3-runtime-test.log \
  milestone3-meson-test.log \
  milestone3-install-test.log \
  milestone3-handshake.log \
  milestone3-fd-transfer.log \
  milestone3-snapshot.log \
  milestone3-malformed.log \
  milestone3-journal.log \
  milestone3-facts.env \
  milestone3-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone3 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone3-summary.json" \
  '"required_base_commit": "d6816484f0293b8b47edf0f13e5da691014e3e7c"'
assert_contains "$artifact_dir/milestone3-summary.json" '"api_version": "0.1.0"'
assert_contains "$artifact_dir/milestone3-summary.json" '"wire_version": "1.0"'
assert_contains "$artifact_dir/milestone3-summary.json" '"contract_roundtrip": "passed"'
assert_contains "$artifact_dir/milestone3-summary.json" '"xorg_xwayland_absent": true'

run_success "$work_dir/milestone3-newer-api.out" \
  env GW_VM_TEST_M3_API_VERSION=0.3.0 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone3-summary.json" '"api_version": "0.3.0"'

run_failure "$work_dir/milestone3-bad-facts.out" \
  env GW_VM_TEST_BAD_M3_FACTS=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone3-summary.json" 'sequence_isolation must be passed'

run_failure "$work_dir/milestone3-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M3_JOURNAL=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone3-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone3-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone3-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=gwipc-m3.service' "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone3-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone4-gate.out" "$gw_vm" milestone4-runtime-test
assert_contains "$work_dir/milestone4-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone4-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone4-runtime-test --yes
assert_contains "$work_dir/milestone4-bad-base.out" \
  'HEAD is not based on required Milestone 4 commit 6080e094c35929d0fb2deb4b31ff4040e392a75e'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone4-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" scenario milestone4-runtime-test --yes
assert_contains "$work_dir/milestone4-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

run_failure "$work_dir/milestone4-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone4-runtime-test.sh"
assert_contains "$work_dir/milestone4-wrapper-gate.out" '--yes'

: >"$command_log"
run_failure "$work_dir/milestone5-gate.out" "$gw_vm" milestone5-runtime-test
assert_contains "$work_dir/milestone5-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone5-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-bad-base.out" \
  'HEAD is not based on required Milestone 5 commit b27a19a869de1d950566b1e3fb9a661e22d5642f'

: >"$command_log"
run_failure "$work_dir/milestone5-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" scenario milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-dirty-source.out" 'requires committed source outside Plans/'

run_failure "$work_dir/milestone5-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone5-runtime-test.sh"
assert_contains "$work_dir/milestone5-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone5.out" "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" '"passed": true'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m5-gwm'
assert_contains "$command_log" 'RuntimeDirectory=glasswyrm-m5'
assert_contains "$command_log" 'run_producer snapshot-reconnect'
assert_contains "$command_log" 'gwipc_policy_c_consumer.c'
assert_file_glob "$artifact_dir/milestone5-policies.tar"

: >"$command_log"
run_failure "$work_dir/milestone5-bad-facts.out" \
  env GW_VM_TEST_BAD_M5_FACTS=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone5-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone5-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M5_JOURNAL=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" 'current invocation journal is missing or empty'

: >"$command_log"
run_failure "$work_dir/milestone5-old-api.out" \
  env GW_VM_TEST_BAD_M5_API=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" 'api_version must be at least 0.3.0'

: >"$command_log"
run_failure "$work_dir/milestone5-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=gwm-m5.service' "$gw_vm" milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone5-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone6-gate.out" "$gw_vm" milestone6-runtime-test
assert_contains "$work_dir/milestone6-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone6-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone6-runtime-test --yes
assert_contains "$work_dir/milestone6-bad-base.out" \
  'HEAD is not based on required Milestone 6 commit 9b9170de569fa112c400780beec3140bd4ef6af1'

run_failure "$work_dir/milestone6-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone6-runtime-test.sh"
assert_contains "$work_dir/milestone6-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone6.out" "$gw_vm" scenario milestone6-runtime-test --yes
assert_contains "$artifact_dir/milestone6-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone6-summary.json" \
  '"required_base_commit": "9b9170de569fa112c400780beec3140bd4ef6af1"'
assert_contains "$artifact_dir/milestone6-summary.json" '"restart_probe": "passed"'
assert_contains "$artifact_dir/milestone6-summary.json" '"xcb_m6_probe": "passed"'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-runtime'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-server'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-server-ipc'
assert_contains "$command_log" 'gwipc_lifecycle_c_consumer.c'
assert_contains "$command_log" 'glasswyrmd-integrated-lifecycle'
assert_contains "$command_log" 'm6_restart_hold_probe'
assert_contains "$command_log" 'systemctl restart gwm-m6.service'
assert_contains "$command_log" 'systemctl restart gwcomp-m6.service'
assert_file_glob "$artifact_dir/milestone6-lifecycle.tar"

: >"$command_log"
run_failure "$work_dir/milestone6-bad-restart.out" \
  env GW_VM_TEST_BAD_M6_FACTS=1 "$gw_vm" milestone6-runtime-test --yes
assert_contains "$artifact_dir/milestone6-summary.json" \
  'restart_probe must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-gate.out" "$gw_vm" milestone7-runtime-test
assert_contains "$work_dir/milestone7-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone7-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$work_dir/milestone7-bad-base.out" \
  'HEAD is not based on required Milestone 7 commit d05dcf2bb979fd82dd5a1dd0a07e34a915ec9746'

run_failure "$work_dir/milestone7-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone7-runtime-test.sh"
assert_contains "$work_dir/milestone7-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone7.out" "$gw_vm" scenario milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone7-summary.json" \
  '"required_base_commit": "d05dcf2bb979fd82dd5a1dd0a07e34a915ec9746"'
assert_contains "$artifact_dir/milestone7-summary.json" '"final_frame_golden": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"connection_survival": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"replay_hash": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"post_restart_hash": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"compiler_c": "gcc test"'
assert_contains "$artifact_dir/milestone7-summary.json" '"scene_manifest": "absent"'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-runtime'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-server'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-server-ipc'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-gwm'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-gwcomp'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-ipc-only'
assert_contains "$command_log" '/run/glasswyrm-m7-gwm/gwm.sock'
assert_contains "$command_log" '/run/glasswyrm-m7-gwcomp/gwcomp.sock'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-dumps'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-scenes'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-control'
assert_contains "$command_log" 'PrivateTmp=yes'
assert_contains "$command_log" 'BindReadOnlyPaths="$runtime_build_dir"'
assert_contains "$command_log" 'BindPaths="$dump_dir"'
assert_contains "$command_log" 'BindPaths="$scene_dir"'
assert_contains "$command_log" '--software-content'
assert_contains "$command_log" 'x11_milestone6_probe'
assert_contains "$command_log" 'gwcomp-golden'
assert_contains "$command_log" 'wm-policy'
assert_contains "$command_log" 'gwipc-malformed'
assert_contains "$command_log" 'x11_milestone7_probe'
assert_contains "$command_log" 'xcb_milestone7_probe'
assert_contains "$command_log" 'm7_restart_hold_probe'
assert_contains "$command_log" 'systemctl restart gwcomp-m7.service'
assert_contains "$command_log" 'systemctl restart gwm-m7.service'
assert_contains "$command_log" 'frame-hashes.json'
assert_contains "$command_log" 'post_restart_frame=$((pre_continue_count + 1))'
assert_contains "$command_log" 'frame_field_at "$post_restart_frame" file'
assert_contains "$command_log" 'cmp "$final_ppm" "$source_dir/tests/fixtures/m7/final.ppm"'
assert_contains "$command_log" 'cmp "$restart_result"'
assert_contains "$command_log" 'journalctl -u glasswyrmd-m7.service -u gwcomp-m7.service'
assert_contains "$command_log" 'archive_files+=(scene.jsonl)'
assert_contains "$command_log" 'scene_manifest=absent'
assert_contains "$command_log" 'milestone7-rendering.tar'
assert_file_glob "$artifact_dir/milestone7-rendering.tar"
if tar -tf "$artifact_dir/milestone7-rendering.tar" | grep -Fx scene.jsonl >/dev/null; then
  fail 'M7 fake rendering archive unexpectedly contains optional scene.jsonl'
fi

: >"$command_log"
run_failure "$work_dir/milestone7-bad-golden.out" \
  env GW_VM_TEST_BAD_M7_FACTS=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" \
  'final_frame_golden must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-bad-replay.out" \
  env GW_VM_TEST_BAD_M7_REPLAY=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" \
  'replay_hash must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-error.out" \
  env GW_VM_TEST_FAIL_MATCH='--software-content' "$gw_vm" milestone7-runtime-test --yes
assert_contains "$work_dir/milestone7-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone7-glasswyrmd-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone8-gate.out" "$gw_vm" milestone8-runtime-test
assert_contains "$work_dir/milestone8-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone8-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone8-runtime-test --yes
assert_contains "$work_dir/milestone8-bad-base.out" \
  'HEAD is not based on required Milestone 8 commit d3f8b4097704c704edf2693b8b213be572fe95e7'

run_failure "$work_dir/milestone8-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone8-runtime-test.sh"
assert_contains "$work_dir/milestone8-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone8.out" "$gw_vm" scenario milestone8-runtime-test --yes
assert_contains "$artifact_dir/milestone8-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone8-summary.json" \
  '"required_base_commit": "d3f8b4097704c704edf2693b8b213be572fe95e7"'
assert_contains "$artifact_dir/milestone8-summary.json" '"event_trace_golden": "passed"'
assert_contains "$artifact_dir/milestone8-summary.json" '"input_connection_survival": "passed"'
assert_contains "$artifact_dir/milestone8-summary.json" '"libinput_absent": "true"'
for expected in /var/tmp/glasswyrm-build-m8-runtime /var/tmp/glasswyrm-build-m8-server /var/tmp/glasswyrm-build-m8-server-ipc /var/tmp/glasswyrm-build-m8-gwm /var/tmp/glasswyrm-build-m8-gwcomp /var/tmp/glasswyrm-build-m8-ipc-only /var/tmp/glasswyrm-m8-dumps /var/tmp/glasswyrm-m8-scenes /var/tmp/glasswyrm-m8-control /var/tmp/glasswyrm-m8-events /run/glasswyrm-m8-input/input.sock gwipc_input_c_consumer.c gwipc-input-contract input-foundation event-router x11_milestone8_probe xcb_milestone8_probe m8_restart_hold_probe 'PrivateDevices=yes' 'RuntimeDirectory=glasswyrm-m8-input' '--synthetic-input-socket' 'systemctl restart gwm-m8.service' 'systemctl restart gwcomp-m8.service'; do
  assert_contains "$command_log" "$expected"
done
assert_contains "$command_log" 'milestone8-glasswyrmd-journal.log'
assert_file_glob "$artifact_dir/milestone8-input-rendering.tar"
for archived in final.ppm selected-runtime.ppm frames.jsonl scene.jsonl raw-little-events.json raw-big-events.json input-acknowledgements.json xcb-result.json result.json SHA256SUMS; do
  tar -tf "$artifact_dir/milestone8-input-rendering.tar" | grep -Fx "$archived" >/dev/null || fail "M8 archive lacks $archived"
done

: >"$command_log"
run_failure "$work_dir/milestone8-bad-evidence.out" \
  env GW_VM_TEST_BAD_M8_FACTS=1 "$gw_vm" milestone8-runtime-test --yes
assert_contains "$artifact_dir/milestone8-summary.json" 'event_trace_golden must be passed'

: >"$command_log"
run_failure "$work_dir/milestone8-error.out" \
  env GW_VM_TEST_FAIL_MATCH='--synthetic-input-socket' "$gw_vm" milestone8-runtime-test --yes
assert_contains "$work_dir/milestone8-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone8-glasswyrmd-journal.log'
assert_contains "$command_log" 'milestone8-input-rendering.tar'

: >"$command_log"
run_failure "$work_dir/milestone9-gate.out" "$gw_vm" milestone9-runtime-test
assert_contains "$work_dir/milestone9-gate.out" '--yes'

: >"$command_log"
run_failure "$work_dir/milestone9-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone9-runtime-test --yes
assert_contains "$work_dir/milestone9-bad-base.out" \
  'HEAD is not based on required Milestone 9 commit 0c694b12a88c941b9ab487c5aee1c805ae7c5d0d'

run_failure "$work_dir/milestone9-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone9-runtime-test.sh"
assert_contains "$work_dir/milestone9-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone9.out" "$gw_vm" scenario milestone9-runtime-test --yes
assert_contains "$artifact_dir/milestone9-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone9-summary.json" \
  '"required_base_commit": "0c694b12a88c941b9ab487c5aee1c805ae7c5d0d"'
assert_contains "$artifact_dir/milestone9-summary.json" '"xeyes": "1.3.1"'
assert_contains "$artifact_dir/milestone9-summary.json" '"xclock": "1.2.0"'
for expected in /var/tmp/glasswyrm-build-m9 /var/tmp/glasswyrm-build-m9-asan /var/tmp/glasswyrm-build-m9-runtime /var/tmp/glasswyrm-build-m9-server /var/tmp/glasswyrm-build-m9-server-ipc /var/tmp/glasswyrm-build-m9-gwm /var/tmp/glasswyrm-build-m9-gwcomp /var/tmp/glasswyrm-build-m9-ipc-only /var/tmp/glasswyrm-m9-clients /var/tmp/glasswyrm-m9-dumps /var/tmp/glasswyrm-m9-scenes /var/tmp/glasswyrm-m9-traces /var/tmp/glasswyrm-m9-control /var/tmp/glasswyrm-m9-artifacts 'source_sha256 = "PENDING"' x11-base/xorg-server media-libs/mesa x11-libs/libdrm dev-libs/libinput x11-libs/libXpm x11-libs/libXi x11-libs/libXft 'gwm-m9' 'gwcomp-m9' 'glasswyrmd-m9' 'reset-failed glasswyrmd-m9.service' '--timeout-multiplier 3' 'PrivateDevices=yes' 'RestrictAddressFamilies=AF_UNIX' '--x11-trace' 'requests.jsonl' '1.3.1' '1.2.0' 'source_url' 'source_sha256' 'curl --fail --location' 'sha256sum --check --status' 'm9-live-xeyes' 'm9-live-xclock-analog' 'm9-live-xclock-digital' 'm9-live-combined' '+shape' '+render' '-analog' '-digital' '-brief' '-twentyfour' '-norender' '-update 0' 'systemctl restart gwcomp-m9.service' 'systemctl restart gwm-m9.service'; do
  assert_contains "$command_log" "$expected"
done
assert_file_glob "$artifact_dir/milestone9-acceptance.tar"

: >"$command_log"
run_failure "$work_dir/milestone9-bad-evidence.out" \
  env GW_VM_TEST_BAD_M9_FACTS=1 "$gw_vm" milestone9-runtime-test --yes
assert_contains "$artifact_dir/milestone9-summary.json" 'xclock_digital must be passed'

: >"$command_log"
run_failure "$work_dir/milestone9-bad-archive.out" \
  env GW_VM_TEST_BAD_M9_ARCHIVE=1 "$gw_vm" milestone9-runtime-test --yes
assert_contains "$work_dir/milestone9-bad-archive.out" 'failed during: artifact-validation'

: >"$command_log"
run_failure "$work_dir/milestone9-error.out" \
  env GW_VM_TEST_FAIL_MATCH='--x11-trace' "$gw_vm" milestone9-runtime-test --yes
assert_contains "$work_dir/milestone9-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone9-glasswyrmd-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone10-gate.out" "$gw_vm" milestone10-runtime-test
assert_contains "$work_dir/milestone10-gate.out" '--yes'

run_failure "$work_dir/milestone10-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone10-runtime-test.sh"
assert_contains "$work_dir/milestone10-wrapper-gate.out" '--yes'

: >"$command_log"
run_failure "$work_dir/milestone10-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10-bad-base.out" \
  'HEAD is not based on required Milestone 10 commit fe0faab39f7a6d28157ee6b96a4f6292a0b7984e'

: >"$command_log"
run_failure "$work_dir/milestone10-dirty.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10-dirty.out" 'requires committed source outside Plans/'

: >"$command_log"
run_failure "$work_dir/milestone10-no-drm.out" \
  env GW_VM_TEST_M10_NO_DRM=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10-no-drm.out" 'no DRM primary node (/dev/dri/card*)'
assert_contains "$work_dir/milestone10-no-drm.out" 'will never install or rebuild a kernel'
assert_not_contains "$command_log" 'x11-libs/libdrm'

: >"$command_log"
run_success "$work_dir/milestone10.out" "$gw_vm" scenario milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10.out" 'reset; milestone9-runtime-test; reset; milestone10-runtime-test'
assert_contains "$artifact_dir/milestone10-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone10-summary.json" \
  '"required_base_commit": "fe0faab39f7a6d28157ee6b96a4f6292a0b7984e"'
for expected in /var/tmp/glasswyrm-build-m10 /var/tmp/glasswyrm-build-m10-asan /var/tmp/glasswyrm-build-m10-runtime /var/tmp/glasswyrm-build-m10-drm-only /var/tmp/glasswyrm-build-m10-headless /var/tmp/glasswyrm-build-m10-server /var/tmp/glasswyrm-build-m10-gwm /var/tmp/glasswyrm-build-m10-ipc-only /var/tmp/glasswyrm-m10-dumps /var/tmp/glasswyrm-m10-scenes /var/tmp/glasswyrm-m10-drm /var/tmp/glasswyrm-m10-control /var/tmp/glasswyrm-m10-artifacts x11-libs/libdrm 'qlist -IC x11-libs/libdrm' 'ldd "$headless/src/gwcomp"' -Ddrm_backend=false -Ddrm_backend=true -Dheadless_backend=false -Dasan=true -Dubsan=true source_layout_test.sh 'drm-ipc-integration.*OK' gw_drm_probe '--device auto' '--require-mode 1024x768' '--snapshot-state' '--expect-active' '--expect-restored' 'getty_unit=getty@${target_vt##*/}.service' gwm-m10 gwcomp-m10 glasswyrmd-m10 '--backend drm' '--mirror-dump-dir' '--drm-report' 'DevicePolicy=closed' 'DeviceAllow=$drm_device rw' 'DeviceAllow=$target_vt rw' 'StandardInput=tty-force' 'TTYReset=yes' 'TTYVHangup=yes' 'TTYVTDisallocate=no' 'VT_GETSTATE=0x5603' 'KDGETMODE=0x4B3B' 'ExecMainStatus 0' 'tests/fixtures/m9/combined.ppm' 'm10_live_combined.sh' 'post-vt-input-complete' 'post-VT xeyes repaint' '/sys/kernel/debug/dri/' screenshot-ready screen-captured screenshot-after-vt-ready screen-after-vt-captured 'chvt 1' 'chvt 2' milestone10-kms-before.json milestone10-kms-after.json milestone10-vt-before.json milestone10-vt-after.json; do
  assert_contains "$command_log" "$expected"
done
assert_contains "$command_log" "header.index('tgid')"
assert_contains "$command_log" 'capture_logind_state'
assert_contains "$command_log" 'restore_logind_state'
assert_contains "$command_log" 'systemd-logind-varlink.socket'
assert_contains "$command_log" 'mask --runtime --now'
assert_contains "$command_log" 'unmask --runtime'
assert_contains "$command_log" '<screenshot> <glasswyrm-test>'
assert_contains "$command_log" 'magick <'
assert_contains "$command_log" 'M10 scene manifest is missing or empty:'
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  "r['root_x']==35 and r['root_y']==55"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  "r['result']=='accepted'"
assert_not_contains "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  "r['delivered_event_count']>0"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  "flip['canonical_hash']!=expected"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  "frame['fnv1a64']==flip['canonical_hash']"
assert_before "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  '>"$control/screenshot-after-vt-ready"' 'touch "$control/post-vt-input"'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone10.sh" \
  'M10 scene manifest is missing or empty:' \
  'tar -cf "$artifact_dir/milestone10-drm-evidence.tar"'
scenario_source=$(sed -n '/name == "m10-xeyes-repaint"/,/^  }/p' \
  "$repo_root/tests/integration/gwinput_m8.cpp")
[[ $(grep -c 'add(K::motion' <<<"$scenario_source") -eq 1 &&
   $(grep -c 'add(K::barrier' <<<"$scenario_source") -eq 1 ]] ||
  fail 'M10 repaint scenario must contain exactly one motion and one barrier'
assert_file_glob "$artifact_dir/milestone10-drm-evidence.tar"
for artifact in milestone10-runtime-test.log milestone10-meson-test.log milestone10-drm-probe.json milestone10-drm-report.jsonl milestone10-kms-before.json milestone10-kms-active.json milestone10-kms-after.json milestone10-vt-before.json milestone10-vt-active.json milestone10-vt-after.json milestone10-apps.log milestone10-screenshot-validation.log milestone10-glasswyrmd-journal.log milestone10-gwm-journal.log milestone10-gwcomp-journal.log milestone10-facts.env milestone10-summary.json milestone10-screen.ppm milestone10-screen-after-vt.ppm milestone10-drm-evidence.tar; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone10 scenario did not create $artifact"
done

: >"$command_log"
run_failure "$work_dir/milestone10-bad-facts.out" \
  env GW_VM_TEST_BAD_M10_FACTS=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$artifact_dir/milestone10-summary.json" 'page_flip must be passed'

: >"$command_log"
run_failure "$work_dir/milestone10-bad-getty.out" \
  env GW_VM_TEST_BAD_M10_GETTY=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$artifact_dir/milestone10-summary.json" \
  'getty active state was not captured and restored'

: >"$command_log"
run_failure "$work_dir/milestone10-missing-scene.out" \
  env GW_VM_TEST_M10_MISSING_SCENE=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$artifact_dir/milestone10-runtime-test.log" \
  'M10 scene manifest is missing or empty: /var/tmp/glasswyrm-m10-scenes/scene.jsonl'
assert_contains "$work_dir/milestone10-missing-scene.out" 'failed during: guest-runtime'
[[ ! -e $artifact_dir/milestone10-drm-evidence.tar ]] ||
  fail 'M10 missing-scene failure unexpectedly produced an evidence archive'

: >"$command_log"
run_failure "$work_dir/milestone10-bad-archive.out" \
  env GW_VM_TEST_BAD_M10_ARCHIVE=1 "$gw_vm" milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10-bad-archive.out" 'failed during: artifact-validation'

: >"$command_log"
run_failure "$work_dir/milestone10-source-layout-error.out" \
  env GW_VM_TEST_FAIL_MATCH=source_layout_test.sh "$gw_vm" milestone10-runtime-test --yes
assert_contains "$work_dir/milestone10-source-layout-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone10-gwcomp-journal.log'

run_failure "$work_dir/milestone11-gate.out" "$gw_vm" milestone11-runtime-test
assert_contains "$work_dir/milestone11-gate.out" '--yes'
run_failure "$work_dir/milestone11-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone11-runtime-test.sh"
assert_contains "$work_dir/milestone11-wrapper-gate.out" '--yes'
run_failure "$work_dir/milestone11-interactive-rerun-gate.out" \
  "$gw_vm" milestone11-interactive-rerun
assert_contains "$work_dir/milestone11-interactive-rerun-gate.out" '--yes'

: >"$command_log"
run_failure "$work_dir/milestone11-interactive-rerun-no-build.out" \
  env GW_VM_TEST_FAIL_MATCH='M11 interactive rerun requires a completed build phase' \
  "$gw_vm" milestone11-interactive-rerun --yes
assert_contains "$work_dir/milestone11-interactive-rerun-no-build.out" \
  'failed during: guest-runtime'

: >"$command_log"
run_failure "$work_dir/milestone11-interactive-rerun-no-runtime-tree.out" \
  env GW_VM_TEST_FAIL_MATCH='requires the existing runtime Meson build directory' \
  "$gw_vm" milestone11-interactive-rerun --yes
assert_contains "$work_dir/milestone11-interactive-rerun-no-runtime-tree.out" \
  'failed during: guest-runtime'

: >"$command_log"
run_success "$work_dir/milestone11-interactive-rerun.out" \
  "$gw_vm" milestone11-interactive-rerun --yes
assert_contains "$work_dir/milestone11-interactive-rerun.out" \
  'Diagnostic only: reusing a prior full M11 build prerequisite'
assert_contains "$work_dir/milestone11-interactive-rerun.out" \
  'full acceptance remains required'
assert_contains "$rerun_artifact_dir/milestone11-interactive-rerun-summary.json" \
  '"diagnostic_only": true'
assert_contains "$rerun_artifact_dir/milestone11-interactive-rerun-summary.json" \
  '"accepted_milestone11_result": false'
assert_contains "$rerun_artifact_dir/milestone11-interactive-rerun-summary.json" \
  '"passed": true'
assert_contains "$rerun_artifact_dir/milestone11-interactive-rerun-summary.json" \
  '"full_build_commit": "75c68dc000000000000000000000000000000000"'
assert_contains "$rerun_artifact_dir/milestone11-interactive-rerun-summary.json" \
  '"runtime_commit": "86dab3c000000000000000000000000000000000"'
[[ -f $rerun_artifact_dir/milestone11-rerun-provenance.env ]] ||
  fail 'interactive rerun did not collect diagnostic provenance'
[[ ! -e $artifact_dir/milestone11-interactive-rerun-summary.json ]] ||
  fail 'interactive rerun summary escaped its diagnostic artifact directory'
assert_contains "$command_log" '<interactive-rerun>'
assert_contains "$command_log" 'if [[ $run_mode == interactive-rerun ]]'
assert_contains "$command_log" 'full_build_matrix=passed'
assert_contains "$command_log" \
  'required_base=9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0'
assert_contains "$command_log" 'xterm_binary_sha256=%s'
assert_contains "$command_log" 'full_build_commit=%s'
assert_contains "$command_log" 'runtime_commit=%s'
assert_contains "$command_log" 'meson-private/coredata.dat'
assert_contains "$command_log" 'meson setup "$runtime" "$source_dir" --reconfigure'
assert_contains "$command_log" 'meson compile -C "$runtime"'
assert_contains "$command_log" \
  'session-launcher subtree-compositor server-lifecycle-state'
assert_contains "$command_log" \
  'local-lifecycle-dispatch gw-uinput-m11-protocol m11-xterm-acceptance'
assert_contains "$command_log" \
  'write_interactive_ready_marker "$full_build_commit" "$tested_commit"'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'meson setup "$runtime" "$source_dir" --reconfigure' \
  'write_interactive_ready_marker "$full_build_commit" "$tested_commit"'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'result[source_layout]=passed' \
  'write_interactive_ready_marker "$tested_commit" "$tested_commit"'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'rm -rf -- "$default" "$build" "$asan" "$clang_build"' \
  'write_interactive_ready_marker "$tested_commit" "$tested_commit"'
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'M11 runtime requires at least 2 GiB free in /var/tmp'

: >"$command_log"
run_failure "$work_dir/milestone11-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone11-runtime-test --yes
assert_contains "$work_dir/milestone11-bad-base.out" \
  'HEAD is not based on required Milestone 11 commit 9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0'

: >"$command_log"
run_failure "$work_dir/milestone11-dirty.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone11-runtime-test --yes
assert_contains "$work_dir/milestone11-dirty.out" 'requires committed source outside Plans/'

: >"$command_log"
run_failure "$work_dir/milestone11-no-uinput.out" \
  env GW_VM_TEST_M11_NO_UINPUT=1 "$gw_vm" milestone11-runtime-test --yes
assert_contains "$work_dir/milestone11-no-uinput.out" '/dev/uinput is unavailable'
assert_contains "$work_dir/milestone11-no-uinput.out" 'CONFIG_INPUT_UINPUT'
assert_not_contains "$command_log" 'dev-libs/libinput'

: >"$command_log"
run_success "$work_dir/milestone11.out" "$gw_vm" scenario milestone11-runtime-test --yes
assert_contains "$work_dir/milestone11.out" \
  'reset; milestone10-runtime-test; reset; milestone11-runtime-test'
assert_contains "$command_log" '<full>'
assert_contains "$artifact_dir/milestone11-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone11-summary.json" \
  '"required_base_commit": "9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0"'
assert_contains "$artifact_dir/milestone11-summary.json" '"core_mapping": "passed"'
assert_contains "$artifact_dir/milestone11-summary.json" '"active_grab": "passed"'
assert_contains "$artifact_dir/milestone11-summary.json" '"automatic_grab": "passed"'
assert_contains "$artifact_dir/milestone11-summary.json" '"client_message": "passed"'
for expected in /var/tmp/glasswyrm-build-m11 /var/tmp/glasswyrm-build-m11-asan \
  /var/tmp/glasswyrm-build-m11-runtime /var/tmp/glasswyrm-build-m11-default \
  /var/tmp/glasswyrm-build-m11-server /var/tmp/glasswyrm-build-m11-gwm \
  /var/tmp/glasswyrm-build-m11-gwcomp /var/tmp/glasswyrm-build-m11-ipc-only \
  /var/tmp/glasswyrm-build-m11-session /var/tmp/glasswyrm-m11-clients \
  /var/tmp/glasswyrm-m11-dumps /var/tmp/glasswyrm-m11-scenes \
  /var/tmp/glasswyrm-m11-input /var/tmp/glasswyrm-m11-control \
  /var/tmp/glasswyrm-m11-artifacts dev-libs/libinput x11-libs/libxkbcommon \
  x11-misc/xkeyboard-config '=x11-terms/xterm-410' 'XTerm(410)' \
  7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f \
  -Dlibinput_backend=false -Dlibinput_backend=true -Ddrm_backend=true \
  -Dasan=true -Dubsan=true 'CC=clang CXX=clang++' source_layout_test.sh \
  gwipc_staged_consumers_test.sh \
  'keyboard-mapping-dispatch' 'grab-state' 'grab-dispatch' \
  "grep -Eq '^[[:space:]]*[^#[:space:]]'" \
  gw_uinput_m11 'serve' 'basic-typing' 'repeat' 'scroll' 'primary-selection' \
  'clipboard-probe' 'move' 'resize' 'close' 'post-vt' 'post-restart' \
  'DeviceAllow=/dev/uinput rw' 'DeviceAllow=$drm_device rw' \
  'DeviceAllow=$target_vt rw' 'DeviceAllow=$keyboard r' \
  'DeviceAllow=$pointer r' glasswyrm-session '--runtime-dir' \
  'StandardInput=tty-force' 'TTYPath=$target_vt' 'TTYReset=yes' \
  'StandardOutput=journal' 'StandardError=journal' \
  'TTYVHangup=yes' 'TTYVTDisallocate=no' \
  '/run/glasswyrm-m11' '--input-device' '--xkb-layout' '--xkb-model' \
  '--drm-api atomic' '--mirror-dump-dir' '--scene-manifest' \
  '--drm-report' '--x11-trace' '-geometry' '80x24+96+96' '-fn' 'fixed' \
  '80x24+384+160' '--move-ready' 'xterm-move-ready.json' \
  'SuccessExitStatus=143' \
  'm11-bashrc' 'chvt 1' 'systemctl restart gwm-m11.service' \
  'systemctl stop gwcomp-m11.service' 'start_gwcomp' \
  milestone11-drm-report-before-restart.jsonl m11_selection_probe \
  m11_xterm_acceptance '--wm-evidence' '--server-journal' \
  screenshot-ready screenshot-after-vt-ready screenshot-after-restart-ready \
  milestone11-libinput-devices.json milestone11-keymap.json \
  milestone11-gwm-bindings.json milestone11-selection-client-message.json \
  milestone11-xterm-trace.raw.jsonl milestone11-xterm-trace.json \
  milestone11-pty-transcript.log milestone11-xterm-geometry.json \
  m11_trace_summarize m11_fixture_validate.py \
  'cmp -s "$source_dir/tests/fixtures/m11/xterm.trace.json"' \
  milestone11-selection.log \
  milestone11-interactive-wm.log milestone11-session-state.log \
  milestone11-drm-report.jsonl; do
  assert_contains "$command_log" "$expected"
done
assert_contains "$command_log" 'rm -rf "$gpg_home"'
assert_contains "$command_log" 'systemctl reset-failed "${m11_units[@]}"'
assert_contains "$command_log" '"$launcher_scenes" "$input" "$control"'
assert_contains "$command_log" 'journalctl --since "@$run_started"'
assert_contains "$command_log" 'xterm_has_pty "$first_xterm_pid"'
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'QXL publishes the accepted KMS buffer'
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "r.get('record')=='vt' and r.get('transition')==transition"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "state=1 result=[12]"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "state=2 result=[12]"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "'active VT':(before['active'][0],after['active'][0])"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "'VT signal':(before['active'][1],after['active'][1])"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "'VT mode':(before['mode'],after['mode'])"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "'KD mode':(before['kd'],after['kd'])"
assert_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'VT open-mask changed (observational only)'
assert_not_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'cmp "$artifact_dir/milestone11-vt-before.json"'
assert_not_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "grep -F 'suspended'"
assert_not_contains "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  "grep -F 'active'"
assert_not_contains "$command_log" '<-u8>'
assert_not_contains "$repo_root/tests/compat/m11/clients.toml" '"-u8"'
for staged_consumer in \
  '0.1|gwipc_transport_c_consumer.c|c' \
  '0.1|gwipc_transport_cpp_consumer.cpp|c++' \
  '0.2|gwipc_c_consumer.c|c' \
  '0.2|gwipc_cpp_consumer.cpp|c++' \
  '0.3|gwipc_policy_c_consumer.c|c' \
  '0.3|gwipc_policy_cpp_consumer.cpp|c++' \
  '0.4|gwipc_lifecycle_c_consumer.c|c' \
  '0.4|gwipc_lifecycle_cpp_consumer.cpp|c++' \
  '0.5|gwipc_input_c_consumer.c|c' \
  '0.5|gwipc_input_cpp_consumer.cpp|c++' \
  '0.6|gwipc_session_c_consumer.c|c' \
  '0.6|gwipc_session_cpp_consumer.cpp|c++'; do
  assert_contains "$repo_root/tests/install/gwipc_staged_consumers_test.sh" \
    "$staged_consumer"
done
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  '[[ -c /dev/uinput ]]' 'emerge --oneshot'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'run_input primary-selection' 'run_input clipboard-probe'
assert_before "$repo_root/tools/gw-vm.d/lib/milestone11.sh" \
  'grep -F '\''"status":"ready"'\'' "$move_ready"' \
  'run_input resize milestone11-interactive-wm.log'
assert_contains "$command_log" '<screenshot> <glasswyrm-test>'
assert_contains "$command_log" 'magick <'
assert_file_glob "$artifact_dir/milestone11-interactive-evidence.tar"
for artifact in milestone11-runtime-test.log milestone11-meson-test.log \
  milestone11-source-layout.log milestone11-libinput-devices.json \
  milestone11-keymap.json milestone11-xterm.log milestone11-xterm-trace.raw.jsonl \
  milestone11-xterm-trace.json milestone11-pty-transcript.log \
  milestone11-xterm-geometry.json \
  milestone11-selection.log milestone11-interactive-wm.log \
  milestone11-gwm-bindings.json milestone11-selection-client-message.json \
  milestone11-session-state.log milestone11-drm-report.jsonl \
  milestone11-drm-report-before-restart.jsonl \
  milestone11-glasswyrmd-journal.log milestone11-gwm-journal.log \
  milestone11-gwcomp-journal.log milestone11-session-journal.log \
  milestone11-facts.env milestone11-summary.json milestone11-desktop.ppm \
  milestone11-desktop-after-vt.ppm milestone11-desktop-after-restart.ppm \
  milestone11-interactive-evidence.tar; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone11 scenario did not create $artifact"
done

: >"$command_log"
run_failure "$work_dir/milestone11-bad-facts.out" \
  env GW_VM_TEST_BAD_M11_FACTS=1 "$gw_vm" milestone11-runtime-test --yes
assert_contains "$artifact_dir/milestone11-summary.json" 'selection_paste must be passed'
assert_contains "$command_log" 'milestone11-session-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone11-bad-archive.out" \
  env GW_VM_TEST_BAD_M11_ARCHIVE=1 "$gw_vm" milestone11-runtime-test --yes
assert_contains "$work_dir/milestone11-bad-archive.out" 'failed during: artifact-validation'

: >"$command_log"
run_failure "$work_dir/milestone12-gate.out" "$gw_vm" milestone12-runtime-test
assert_contains "$work_dir/milestone12-gate.out" \
  "Action 'milestone12-runtime-test' requires --yes"
[[ ! -s $command_log ]] || fail 'M12 approval gate reached a host transport command'

run_failure "$work_dir/milestone12-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone12-runtime-test.sh"
assert_contains "$work_dir/milestone12-wrapper-gate.out" \
  "Action 'milestone12-runtime-test' requires --yes"

m12_lib=$repo_root/tools/gw-vm.d/lib/milestone12.sh
for expected in ae6b6c93a29a1fb985dcea8455650d15c0fec364 \
  /var/tmp/glasswyrm-build-m12 /var/tmp/glasswyrm-build-m12-asan \
  /var/tmp/glasswyrm-build-m12-software /var/tmp/glasswyrm-build-m12-gles \
  /var/tmp/glasswyrm-build-m12-default /var/tmp/glasswyrm-build-m12-server \
  /var/tmp/glasswyrm-build-m12-gwm /var/tmp/glasswyrm-build-m12-gwcomp \
  /var/tmp/glasswyrm-build-m12-ipc-only /var/tmp/glasswyrm-m12-clients \
  /var/tmp/glasswyrm-m12-dumps /var/tmp/glasswyrm-m12-scenes \
  /var/tmp/glasswyrm-m12-renderer /var/tmp/glasswyrm-m12-control \
  /var/tmp/glasswyrm-m12-artifacts dev-libs/libinput \
  x11-misc/xkeyboard-config 'media-libs/libglvnd X' \
  'media-libs/mesa -llvm' 2.32.10 \
  5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 \
  -Dexperimental=false -Drender_gl=false -Drender_gl=true \
  '--game-compat' '--disable-extension' MIT-SHM run_workloads.py \
  milestone12-software.ppm milestone12-gles.ppm milestone12-screen.ppm \
  milestone12-gles-screen.ppm milestone12-efficient-sdl-evidence.tar \
  'script="$(milestone12_guest_script; milestone12_guest_script_tail)"'; do
  assert_contains "$m12_lib" "$expected"
done
assert_contains "$repo_root/tests/compat/m12/acquire_sdl.sh" \
  'https://www.libsdl.org/release/SDL2-2.32.10.tar.gz'
assert_contains "$repo_root/tests/compat/m12/acquire_sdl.sh" \
  '--retry-all-errors'
assert_before "$m12_lib" \
  'rm -rf -- "$default" "$asan" "${build}-clang" "$server" "$gwm_build"' \
  'failure_stage=state-capture'
assert_contains "$m12_lib" \
  'M12 runtime requires at least 2 GiB free in /var/tmp'
assert_before "$m12_lib" \
  'emerge --oneshot --noreplace' \
  'pkg-config --exists egl glesv2 gbm'
assert_before "$m12_lib" \
  'pkg-config --exists egl glesv2 gbm' \
  'failure_stage=sdl-acquisition'
assert_contains "$m12_lib" \
  'after dependency installation'

assert_not_contains "$work_dir/help.out" 'ssh COMMAND'

printf 'gw-vm CLI tests passed\n'
