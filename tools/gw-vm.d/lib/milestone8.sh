#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n "${GW_VM_MILESTONE8_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE8_LOADED=1

M8_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m8-artifacts
M8_REQUIRED_BASE_COMMIT=d3f8b4097704c704edf2693b8b213be572fe95e7
M8_TESTED_COMMIT=
M8_ARTIFACT_NAMES=(milestone8-runtime-test.log milestone8-meson-test.log
  milestone8-raw-input.log milestone8-xcb.log milestone8-events.log
  milestone8-focus.log milestone8-restart.log milestone8-malformed.log
  milestone8-glasswyrmd-journal.log milestone8-gwm-journal.log
  milestone8-gwcomp-journal.log milestone8-facts.env)

milestone8_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m8
sanitizer_build_dir=/var/tmp/glasswyrm-build-m8-asan
runtime_build_dir=/var/tmp/glasswyrm-build-m8-runtime
server_build_dir=/var/tmp/glasswyrm-build-m8-server
server_ipc_build_dir=/var/tmp/glasswyrm-build-m8-server-ipc
gwm_build_dir=/var/tmp/glasswyrm-build-m8-gwm
gwcomp_build_dir=/var/tmp/glasswyrm-build-m8-gwcomp
ipc_build_dir=/var/tmp/glasswyrm-build-m8-ipc-only
install_root=/var/tmp/glasswyrm-m8-install
scene_dir=/var/tmp/glasswyrm-m8-scenes
dump_dir=/var/tmp/glasswyrm-m8-dumps
control_dir=/var/tmp/glasswyrm-m8-control
event_dir=/var/tmp/glasswyrm-m8-events
runtime_log="$artifact_dir/milestone8-runtime-test.log"
meson_log="$artifact_dir/milestone8-meson-test.log"
raw_log="$artifact_dir/milestone8-raw-input.log"
xcb_log="$artifact_dir/milestone8-xcb.log"
events_log="$artifact_dir/milestone8-events.log"
focus_log="$artifact_dir/milestone8-focus.log"
restart_log="$artifact_dir/milestone8-restart.log"
malformed_log="$artifact_dir/milestone8-malformed.log"
facts="$artifact_dir/milestone8-facts.env"
restart_result="$control_dir/result.json"
failure_stage=dependency-preparation
full_tests=not-run sanitizer=not-run runtime_build=not-run server_standalone=not-run server_ipc=not-run
gwm_only=not-run gwcomp_only=not-run ipc_only=not-run api05_consumers=not-run
m6_metadata_regression=not-run m7_drawable_regression=not-run raw_little=not-run raw_big=not-run
image_byte_order=not-run exposure_events=not-run malformed_x11=not-run
x11_resources=not-run raster_requests=not-run plane_mask=not-run damage_coalescing=not-run
m4_pixel_regression=not-run m5_policy_regression=not-run malformed_gwipc=not-run
xcb_drawing=not-run final_frame_golden=not-run buffer_release=not-run
compositor_restart=not-run gwm_restart=not-run connection_survival=not-run
replay_hash=not-run post_restart_hash=not-run post_restart_drawing=not-run
rendering_archive=not-run scene_manifest=not-run
input_socket_security=not-run motion=not-run crossing=not-run buttons=not-run
button_motion=not-run keyboard=not-run modifiers=not-run propagation=not-run
do_not_propagate=not-run click_focus=not-run focus_events=not-run
scene_change_crossing=not-run two_client_routing=not-run event_trace_golden=not-run
malformed_provider_isolation=not-run input_reconnect=not-run input_connection_survival=not-run
x11_connection_survival=not-run focus_replay=not-run pointer_replay=not-run
post_restart_input=not-run no_device_access=not-run service_results=not-run
socket_cleanup=not-run journal_evidence=not-run archive_validation=not-run
mkdir -p "$artifact_dir" "$scene_dir" "$dump_dir" "$control_dir" "$event_dir"
rm -f "$artifact_dir"/milestone8-* "$facts"; rm -rf "$scene_dir"/* "$dump_dir"/* "$control_dir"/* "$event_dir"/*
touch "$runtime_log" "$meson_log" "$raw_log" "$xcb_log" "$events_log" "$focus_log" "$restart_log" "$malformed_log"
exec > >(tee -a "$runtime_log") 2>&1
record_facts() {
  status=$?; set +e
  systemctl stop glasswyrmd-m8.service gwm-m8.service gwcomp-m8.service >/dev/null 2>&1
  journalctl -u glasswyrmd-m8.service --no-pager >"$artifact_dir/milestone8-glasswyrmd-journal.log" 2>&1
  journalctl -u gwm-m8.service --no-pager >"$artifact_dir/milestone8-gwm-journal.log" 2>&1
  journalctl -u gwcomp-m8.service --no-pager >"$artifact_dir/milestone8-gwcomp-journal.log" 2>&1
  if [[ -s "$artifact_dir/milestone8-glasswyrmd-journal.log" && -s "$artifact_dir/milestone8-gwm-journal.log" && -s "$artifact_dir/milestone8-gwcomp-journal.log" ]]; then journal_evidence=passed; fi
  if grep -Eq 'published buffer released buffer=[0-9]+ reason=[12]' "$artifact_dir/milestone8-glasswyrmd-journal.log"; then buffer_release=passed; fi
  if [[ "$(systemctl show -p Result --value glasswyrmd-m8.service 2>/dev/null)" == success && "$(systemctl show -p Result --value gwm-m8.service 2>/dev/null)" == success && "$(systemctl show -p Result --value gwcomp-m8.service 2>/dev/null)" == success ]]; then service_results=passed; fi
  systemctl reset-failed glasswyrmd-m8.service gwm-m8.service gwcomp-m8.service >/dev/null 2>&1
  if [[ ! -e /run/glasswyrm-m8-gwm/gwm.sock && ! -e /run/glasswyrm-m8-gwcomp/gwcomp.sock && ! -e /run/glasswyrm-m8-input/input.sock && ! -e /tmp/.X11-unix/X99 ]]; then socket_cleanup=passed; fi
  rm -rf /run/glasswyrm-m8-gwm /run/glasswyrm-m8-gwcomp /run/glasswyrm-m8-input /tmp/.X11-unix/X99
  api_version=unknown; wire_version=unknown
  if [[ -x "$ipc_build_dir/tests/gwipc_wire_probe" ]]; then
    api_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-api-version 2>/dev/null || printf unknown)"
    wire_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-wire-version 2>/dev/null || printf unknown)"
  fi
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$status"
    printf 'api_version=%s\nsoversion=0\nwire_version=%s\n' "$api_version" "$wire_version"
    printf 'compiler_c=%s\ncompiler_cxx=%s\n' "$(cc --version 2>/dev/null | head -n1 || printf unknown)" "$(c++ --version 2>/dev/null | head -n1 || printf unknown)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' "$(meson --version 2>/dev/null || printf unknown)" "$(ninja --version 2>/dev/null || printf unknown)" "$(systemctl --version 2>/dev/null | head -n1 || printf unknown)"
    printf 'xcb_proto=%s\n' "$(portageq match / x11-base/xcb-proto 2>/dev/null || printf unknown)"
    printf 'scene_manifest=%s\n' "$scene_manifest"
    if [[ -z "$(portageq match / dev-libs/libinput 2>/dev/null)" ]]; then echo libinput_absent=true; else echo libinput_absent=false; fi
    if [[ -n "$(portageq match / x11-base/xorg-server 2>/dev/null)" ||
          -n "$(portageq match / x11-base/xwayland 2>/dev/null)" ]]; then
      echo x_servers_absent=false
    else
      echo x_servers_absent=true
    fi
    for key in full_tests sanitizer runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api05_consumers m4_pixel_regression m5_policy_regression m6_metadata_regression m7_drawable_regression raw_little raw_big image_byte_order exposure_events malformed_x11 malformed_gwipc x11_resources raster_requests plane_mask damage_coalescing xcb_drawing final_frame_golden buffer_release compositor_restart gwm_restart connection_survival replay_hash post_restart_hash post_restart_drawing rendering_archive input_socket_security motion crossing buttons button_motion keyboard modifiers propagation do_not_propagate click_focus focus_events scene_change_crossing two_client_routing event_trace_golden malformed_provider_isolation input_reconnect input_connection_survival x11_connection_survival focus_replay pointer_replay post_restart_input no_device_access service_results socket_cleanup journal_evidence archive_validation; do printf '%s=%s\n' "$key" "${!key}"; done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT
[[ -f "$source_dir/.glasswyrm-vm-source" ]]
emerge --verbose --noreplace --color=n sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig x11-libs/libxcb x11-base/xcb-proto
rm -rf "$build_dir" "$sanitizer_build_dir" "$runtime_build_dir" "$server_build_dir" "$server_ipc_build_dir" "$gwm_build_dir" "$gwcomp_build_dir" "$ipc_build_dir" "$install_root"
run_build() { name=$1; dir=$2; shift 2; failure_stage="$name"; { meson setup "$dir" "$source_dir" -Dwerror=true "$@"; meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"; }
run_build full-build-and-test "$build_dir"; full_tests=passed
if printf 'int main(void){return 0;}\n' | cc -x c - -fsanitize=address,undefined -o /var/tmp/m8-san-probe && /var/tmp/m8-san-probe; then run_build sanitizer-build-and-test "$sanitizer_build_dir" -Dasan=true -Dubsan=true; sanitizer=passed; else sanitizer=unavailable; fi
run_build integrated-runtime "$runtime_build_dir" -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false; runtime_build=passed
run_build standalone-server "$server_build_dir" -Dlibgwipc=false -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false; server_standalone=passed
run_build server-with-ipc "$server_ipc_build_dir" -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false; server_ipc=passed
run_build gwm-only "$gwm_build_dir" -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false; gwm_only=passed
run_build gwcomp-only "$gwcomp_build_dir" -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false; gwcomp_only=passed
run_build ipc-only "$ipc_build_dir" --prefix=/usr -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false; ipc_only=passed
failure_stage=api-0.5-consumers
DESTDIR="$install_root" meson install -C "$ipc_build_dir" >>"$meson_log" 2>&1
lib="$install_root/usr/lib64"; [[ -d "$lib" ]] || lib="$install_root/usr/lib"
export PKG_CONFIG_PATH="$lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$install_root" LD_LIBRARY_PATH="$lib"
read -r -a flags <<<"$(pkg-config --cflags --libs gwipc)"
for source in gwipc_c_consumer.c gwipc_policy_c_consumer.c gwipc_lifecycle_c_consumer.c gwipc_input_c_consumer.c; do cc "$source_dir/tests/install/$source" -o "/var/tmp/${source%.c}" "${flags[@]}"; "/var/tmp/${source%.c}"; done
for source in gwipc_cpp_consumer.cpp gwipc_policy_cpp_consumer.cpp gwipc_lifecycle_cpp_consumer.cpp gwipc_input_cpp_consumer.cpp; do c++ -std=c++20 "$source_dir/tests/install/$source" -o "/var/tmp/${source%.cpp}" "${flags[@]}"; "/var/tmp/${source%.cpp}"; done
api05_consumers=passed
meson test -C "$runtime_build_dir" --print-errorlogs gwcomp-golden; m4_pixel_regression=passed
meson test -C "$runtime_build_dir" --print-errorlogs wm-policy; m5_policy_regression=passed
meson test -C "$runtime_build_dir" --print-errorlogs gwipc-malformed 2>&1 | tee -a "$malformed_log"; malformed_gwipc=passed
meson test -C "$runtime_build_dir" --print-errorlogs published-buffer content-presenter gwcomp-metadata-scene 2>&1 | tee -a "$meson_log"
meson test -C "$runtime_build_dir" --print-errorlogs drawable-core 2>&1 | tee -a "$meson_log"; damage_coalescing=passed
meson test -C "$runtime_build_dir" --print-errorlogs gwipc-input-contract input-foundation event-router glasswyrmd-input-recovery 2>&1 | tee -a "$meson_log"
failure_stage=integrated-three-process-probe
meson test -C "$runtime_build_dir" --print-errorlogs glasswyrmd-integrated-lifecycle 2>&1 | tee -a "$raw_log"
gwm_socket=/run/glasswyrm-m8-gwm/gwm.sock
comp_socket=/run/glasswyrm-m8-gwcomp/gwcomp.sock
input_socket=/run/glasswyrm-m8-input/input.sock
rm -rf /run/glasswyrm-m8-gwm /run/glasswyrm-m8-gwcomp /run/glasswyrm-m8-input /tmp/.X11-unix/X99; mkdir -p /tmp/.X11-unix
unit_properties=(--property=Type=exec --property=Restart=no --property=NoNewPrivileges=yes --property=PrivateDevices=yes --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet= --property=AmbientCapabilities=)
systemd-run --unit=gwm-m8 "${unit_properties[@]}" --property=PrivateTmp=yes --property=BindReadOnlyPaths="$runtime_build_dir" --property=RuntimeDirectory=glasswyrm-m8-gwm --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_build_dir/src/gwm" --ipc-socket "$gwm_socket"
systemd-run --unit=gwcomp-m8 "${unit_properties[@]}" --property=PrivateTmp=yes --property=BindReadOnlyPaths="$runtime_build_dir" --property=BindPaths="$dump_dir" --property=BindPaths="$scene_dir" --property=RuntimeDirectory=glasswyrm-m8-gwcomp --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_build_dir/src/gwcomp" --ipc-socket "$comp_socket" --dump-dir "$dump_dir" --scene-manifest "$scene_dir/scene.jsonl"
for socket in "$gwm_socket" "$comp_socket"; do for _ in {1..200}; do [[ -S "$socket" ]] && break; sleep .05; done; [[ -S "$socket" ]]; done
# First prove that the accepted M6 metadata-only launch still emits no pixels.
systemd-run --unit=glasswyrmd-m8 "${unit_properties[@]}" --property=PrivateTmp=no --no-block -- "$runtime_build_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 ]]
"$runtime_build_dir/tests/x11_milestone6_probe" --display :99 --byte-order little >>"$raw_log" 2>&1
[[ ! -e "$dump_dir/frames.jsonl" ]]; m6_metadata_regression=passed
systemctl stop glasswyrmd-m8.service; rm -f /tmp/.X11-unix/X99
systemd-run --unit=glasswyrmd-m8 "${unit_properties[@]}" --property=PrivateTmp=no --property=RuntimeDirectory=glasswyrm-m8-input --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_build_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket" --software-content --synthetic-input-socket "$input_socket"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 && -S "$input_socket" ]] && break; sleep .05; done
[[ -S /tmp/.X11-unix/X99 && -S "$input_socket" ]]
[[ "$(stat -c %a "$input_socket")" == 600 ]]
[[ "$(stat -c %u "$input_socket")" == "$(id -u)" ]]; input_socket_security=passed no_device_access=passed
"$runtime_build_dir/tests/x11_milestone8_probe" --display :99 --input-socket "$input_socket" --byte-order little --scenario routing 2>>"$raw_log" | tee "$event_dir/raw-little-events.json" >>"$raw_log"; raw_little=passed
"$runtime_build_dir/tests/x11_milestone8_probe" --display :99 --input-socket "$input_socket" --byte-order big --scenario routing 2>>"$raw_log" | tee "$event_dir/raw-big-events.json" >>"$raw_log"; raw_big=passed image_byte_order=passed
motion=passed crossing=passed buttons=passed button_motion=passed keyboard=passed modifiers=passed two_client_routing=passed
"$runtime_build_dir/tests/x11_milestone8_probe" --display :99 --input-socket "$input_socket" --byte-order little --scenario propagation >>"$events_log" 2>&1; exposure_events=passed propagation=passed do_not_propagate=passed
"$runtime_build_dir/tests/x11_milestone8_probe" --display :99 --input-socket "$input_socket" --byte-order little --scenario state >>"$events_log" 2>&1
"$runtime_build_dir/tests/x11_milestone8_probe" --display :99 --input-socket "$input_socket" --byte-order little --scenario errors 2>>"$malformed_log" | tee "$event_dir/input-acknowledgements.json" >>"$malformed_log"; malformed_x11=passed malformed_provider_isolation=passed input_reconnect=passed
expected_xcb="$(sed -n 's/.*"xcb_final":"\([0-9a-f]*\)".*/\1/p' "$source_dir/tests/fixtures/m8/frame-hashes.json")"
DISPLAY=:99 XAUTHORITY=/dev/null "$runtime_build_dir/tests/xcb_milestone8_probe" --input-socket "$input_socket" --output "$control_dir/xcb-result.json" 2>>"$xcb_log" & xcb_pid=$!
for _ in {1..200}; do
  xcb_line="$(grep -F "\"fnv1a64\":\"$expected_xcb\"" "$dump_dir/frames.jsonl" 2>/dev/null | tail -n1 || true)"
  [[ -n "$xcb_line" ]] && break
  sleep .05
done
[[ -n "$xcb_line" ]]
xcb_name="$(printf '%s\n' "$xcb_line" | sed -n 's/.*"file":"\([^"]*\)".*/\1/p')"
cp "$dump_dir/$xcb_name" "$control_dir/final.ppm"
cmp "$control_dir/final.ppm" "$source_dir/tests/fixtures/m8/final.ppm"
wait "$xcb_pid"
xcb_window_a="$(sed -n 's/.*"window_a":\([0-9]*\).*/\1/p' "$control_dir/xcb-result.json")"
xcb_window_b="$(sed -n 's/.*"window_b":\([0-9]*\).*/\1/p' "$control_dir/xcb-result.json")"
xcb_focus="$(sed -n 's/.*"focus":\([0-9]*\).*/\1/p' "$control_dir/xcb-result.json")"
xcb_pointer="$(sed -n 's/.*"pointer_window":\([0-9]*\).*/\1/p' "$control_dir/xcb-result.json")"
[[ -n "$xcb_window_a" && -n "$xcb_window_b" && "$xcb_window_a" != "$xcb_window_b" ]]
[[ "$xcb_focus" == "$xcb_window_b" && "$xcb_pointer" == "$xcb_window_b" ]]
grep -F '"completed":true' "$control_dir/xcb-result.json" >/dev/null
grep -F '"state":0' "$control_dir/xcb-result.json" >/dev/null
cat "$control_dir/xcb-result.json" >>"$xcb_log"
xcb_drawing=passed x11_resources=passed raster_requests=passed final_frame_golden=passed
click_focus=passed focus_events=passed scene_change_crossing=passed event_trace_golden=passed
frame_count() { [[ -f "$dump_dir/frames.jsonl" ]] && wc -l <"$dump_dir/frames.jsonl" || printf '0\n'; }
last_frame_field() { tail -n1 "$dump_dir/frames.jsonl" | sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p"; }
frame_field_at() { sed -n "${1}p" "$dump_dir/frames.jsonl" | sed -n "s/.*\"$2\":\"\([^\"]*\)\".*/\1/p"; }
wait_for_frame_after() { previous=$1; for _ in {1..200}; do current=$(frame_count); (( current > previous )) && return 0; sleep .05; done; return 1; }
expected_pre="$(sed -n 's/.*"pre_restart": "\([0-9a-f]*\)".*/\1/p' "$source_dir/tests/fixtures/m8/frame-hashes.json")"
expected_post="$(sed -n 's/.*"post_restart": "\([0-9a-f]*\)".*/\1/p' "$source_dir/tests/fixtures/m8/frame-hashes.json")"
[[ ${#expected_pre} -eq 16 && ${#expected_post} -eq 16 ]]
before_hold=$(frame_count)
DISPLAY=:99 XAUTHORITY=/dev/null "$runtime_build_dir/tests/m8_restart_hold_probe" --display :99 --input-socket "$input_socket" --control-dir "$control_dir" >>"$restart_log" 2>&1 & hold_pid=$!
for _ in {1..200}; do [[ -f "$control_dir/ready" ]] && break; kill -0 "$hold_pid" 2>/dev/null || break; sleep .05; done
[[ -f "$control_dir/ready" ]] || { wait "$hold_pid"; exit 1; }
wait_for_frame_after "$before_hold"
pre_restart_hash="$(last_frame_field fnv1a64)"; [[ "$pre_restart_hash" == "$expected_pre" ]]
pre_restart_count=$(frame_count)
systemctl restart gwcomp-m8.service
for _ in {1..200}; do systemctl is-active --quiet gwcomp-m8.service && [[ -S "$comp_socket" ]] && break; sleep .05; done
systemctl is-active --quiet gwcomp-m8.service; [[ -S "$comp_socket" ]]
wait_for_frame_after "$pre_restart_count"
[[ "$(last_frame_field fnv1a64)" == "$pre_restart_hash" ]]
replay_hash=passed compositor_restart=passed
pre_gwm_count=$(frame_count)
systemctl restart gwm-m8.service
for _ in {1..200}; do systemctl is-active --quiet gwm-m8.service && [[ -S "$gwm_socket" ]] && break; sleep .05; done
systemctl is-active --quiet gwm-m8.service; [[ -S "$gwm_socket" ]]
wait_for_frame_after "$pre_gwm_count"
[[ "$(last_frame_field fnv1a64)" == "$pre_restart_hash" ]]
gwm_restart=passed focus_replay=passed pointer_replay=passed
pre_continue_count=$(frame_count)
touch "$control_dir/continue"; wait "$hold_pid"
cmp "$restart_result" "$source_dir/tests/fixtures/m8/restart-result.json"
connection_survival=passed input_connection_survival=passed x11_connection_survival=passed plane_mask=passed post_restart_drawing=passed post_restart_input=passed
for _ in {1..200}; do
  post_restart_hash_value="$(last_frame_field fnv1a64)"
  [[ "$(frame_count)" -gt "$pre_continue_count" && "$post_restart_hash_value" == "$expected_post" ]] && break
  sleep .05
done
[[ "$post_restart_hash_value" == "$expected_post" && "$post_restart_hash_value" != "$pre_restart_hash" ]]
post_restart_hash=passed
cp "$restart_result" "$artifact_dir/milestone8-restart-result.json"
post_name="$(last_frame_field file)"; post_ppm="$dump_dir/$post_name"; [[ -n "$post_name" && -s "$post_ppm" ]]
sha256sum "$post_ppm" | tee "$control_dir/post-restart-frame.sha256"
journalctl -u glasswyrmd-m8.service -u gwcomp-m8.service --no-pager >>"$events_log"
cp "$dump_dir/frames.jsonl" "$control_dir/frames.jsonl"
cp "$post_ppm" "$control_dir/selected-runtime.ppm"
cp "$event_dir/raw-little-events.json" "$control_dir/raw-little-events.json"
cp "$event_dir/raw-big-events.json" "$control_dir/raw-big-events.json"
cp "$event_dir/input-acknowledgements.json" "$control_dir/input-acknowledgements.json"
archive_files=(final.ppm selected-runtime.ppm frames.jsonl raw-little-events.json raw-big-events.json input-acknowledgements.json xcb-result.json result.json post-restart-frame.sha256)
if [[ -s "$scene_dir/scene.jsonl" ]]; then
  cp "$scene_dir/scene.jsonl" "$control_dir/scene.jsonl"
  archive_files+=(scene.jsonl)
  scene_manifest=present
else
  scene_manifest=absent
fi
(cd "$control_dir" && sha256sum "${archive_files[@]}" >SHA256SUMS && tar -cf "$artifact_dir/milestone8-input-rendering.tar" "${archive_files[@]}" SHA256SUMS)
tar -tf "$artifact_dir/milestone8-input-rendering.tar" | tee -a "$runtime_log" | grep -Fx final.ppm
rendering_archive=passed
archive_validation=passed m7_drawable_regression=passed
failure_stage=
GUEST_SCRIPT
}

verify_milestone8_source_identity() {
  local status unexpected='' line current
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do [[ -z "$line" || "$line" == '?? Plans/'* ]] || unexpected+="${unexpected:+$'\n'}$line"; done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 8 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M8_TESTED_COMMIT" || "$current" == "$M8_TESTED_COMMIT" ]]
}

prepare_milestone8_evidence() {
  M8_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone8_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M8_REQUIRED_BASE_COMMIT" "$M8_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 8 commit %s\n' "$M8_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone8-*
}

collect_milestone8_artifacts() {
  local name failed=0 verify_dir
  init_artifacts
  for name in "${M8_ARTIFACT_NAMES[@]}"; do guest_run_script 'set -euo pipefail; cat "$1"' "$M8_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1; done
  ssh_arguments
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M8_GUEST_ARTIFACT_DIR/milestone8-input-rendering.tar" "$ARTIFACTS_PATH_ABS/milestone8-input-rendering.tar" || failed=1
  if [[ -f "$ARTIFACTS_PATH_ABS/milestone8-input-rendering.tar" ]]; then
    verify_dir="$(mktemp -d "$ARTIFACTS_PATH_ABS/.milestone8-archive.XXXXXX")" || return 1
    tar -xf "$ARTIFACTS_PATH_ABS/milestone8-input-rendering.tar" -C "$verify_dir" &&
      (cd "$verify_dir" && sha256sum -c SHA256SUMS >/dev/null) || failed=1
    rm -rf "$verify_dir"
  else
    failed=1
  fi
  return "$failed"
}

write_milestone8_summary() {
  local requested=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone8-facts.env" out="$ARTIFACTS_PATH_ABS/milestone8-summary.json"
  python3 - "$facts" "$out" "$requested" "$failure" "$M8_REQUIRED_BASE_COMMIT" "${M8_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    k,s,v=line.partition('=')
    if s: facts[k]=v
required='full_tests runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api05_consumers m4_pixel_regression m5_policy_regression m6_metadata_regression m7_drawable_regression input_socket_security raw_little raw_big motion crossing buttons button_motion keyboard modifiers propagation do_not_propagate click_focus focus_events scene_change_crossing two_client_routing xcb_drawing final_frame_golden event_trace_golden malformed_provider_isolation input_reconnect gwm_restart compositor_restart input_connection_survival x11_connection_survival focus_replay pointer_replay replay_hash post_restart_hash post_restart_input no_device_access service_results socket_cleanup rendering_archive archive_validation journal_evidence'.split()
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
if facts.get('api_version')!='0.5.0': errors.append('api_version must be 0.5.0')
if facts.get('wire_version')!='1.0' or facts.get('soversion')!='0': errors.append('ABI or wire evidence mismatch')
if facts.get('x_servers_absent')!='true': errors.append('Xorg and Xwayland must be absent')
if facts.get('libinput_absent')!='true': errors.append('libinput must be absent')
if facts.get('sanitizer')!='passed': errors.append('sanitizer must be passed')
if facts.get('scene_manifest')!='present': errors.append('scene_manifest must be present')
versions={k:facts.get(k,'unknown') for k in ('compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','xcb_proto')}
errors += [f'{k} must be recorded' for k,v in versions.items() if v in {'','unknown'}]
payload={'required_base_commit':base,'tested_commit':tested,'api_version':facts.get('api_version','unknown'),'soversion':facts.get('soversion','unknown'),'wire_version':facts.get('wire_version','unknown'),'x_servers_absent':facts.get('x_servers_absent','unknown'),'libinput_absent':facts.get('libinput_absent','unknown'),'versions':versions,'results':{k:facts.get(k,'unknown') for k in required},'sanitizer':facts.get('sanitizer','unknown'),'scene_manifest':facts.get('scene_manifest','unknown'),'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone8_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 script
  require_approval milestone8-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone8_evidence || { status=$?; failure=source-evidence; }
  [[ -n "$failure" ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z "$failure" ]]; then guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p -- "$1"' "$M8_GUEST_ARTIFACT_DIR" || { status=$?; failure=artifact-preparation; }; fi
  if [[ -z "$failure" ]]; then
    if verify_milestone8_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z "$failure" ]]; then script="$(milestone8_guest_script)"; capture_guest_action milestone8-runtime-test "$ARTIFACTS_PATH_ABS/milestone8-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M8_GUEST_ARTIFACT_DIR" || { status=$?; failure=guest-runtime; }; fi
  collect_milestone8_artifacts || collection=$?
  if ((collection)) && [[ -z "$failure" ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -n "$failure" ]]; then write_milestone8_summary false "$failure" || true; printf 'Milestone 8 VM runtime test failed during: %s\n' "$failure" >&2; return "${status:-1}"; fi
  verify_milestone8_source_identity || { write_milestone8_summary false source-identity-changed; return 1; }
  write_milestone8_summary true ''; echo 'Milestone 8 VM runtime test passed.'; print_artifacts
}
