#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n "${GW_VM_MILESTONE7_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE7_LOADED=1

M7_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m7-artifacts
M7_REQUIRED_BASE_COMMIT=d05dcf2bb979fd82dd5a1dd0a07e34a915ec9746
M7_TESTED_COMMIT=
M7_ARTIFACT_NAMES=(milestone7-runtime-test.log milestone7-meson-test.log
  milestone7-raw-x11.log milestone7-xcb.log milestone7-exposure.log
  milestone7-buffer-release.log milestone7-restart.log milestone7-malformed.log
  milestone7-glasswyrmd-journal.log milestone7-gwm-journal.log
  milestone7-gwcomp-journal.log milestone7-facts.env)

milestone7_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m7
sanitizer_build_dir=/var/tmp/glasswyrm-build-m7-asan
runtime_build_dir=/var/tmp/glasswyrm-build-m7-runtime
server_build_dir=/var/tmp/glasswyrm-build-m7-server
server_ipc_build_dir=/var/tmp/glasswyrm-build-m7-server-ipc
gwm_build_dir=/var/tmp/glasswyrm-build-m7-gwm
gwcomp_build_dir=/var/tmp/glasswyrm-build-m7-gwcomp
ipc_build_dir=/var/tmp/glasswyrm-build-m7-ipc-only
install_root=/var/tmp/glasswyrm-m7-install
scene_dir=/var/tmp/glasswyrm-m7-scenes
dump_dir=/var/tmp/glasswyrm-m7-dumps
control_dir=/var/tmp/glasswyrm-m7-control
runtime_log="$artifact_dir/milestone7-runtime-test.log"
meson_log="$artifact_dir/milestone7-meson-test.log"
raw_log="$artifact_dir/milestone7-raw-x11.log"
xcb_log="$artifact_dir/milestone7-xcb.log"
exposure_log="$artifact_dir/milestone7-exposure.log"
release_log="$artifact_dir/milestone7-buffer-release.log"
restart_log="$artifact_dir/milestone7-restart.log"
malformed_log="$artifact_dir/milestone7-malformed.log"
facts="$artifact_dir/milestone7-facts.env"
restart_result="$control_dir/result.json"
failure_stage=dependency-preparation
full_tests=not-run sanitizer=not-run runtime_build=not-run server_standalone=not-run server_ipc=not-run
gwm_only=not-run gwcomp_only=not-run ipc_only=not-run api04_consumers=not-run
m6_metadata_regression=not-run raw_little=not-run raw_big=not-run
image_byte_order=not-run exposure_events=not-run malformed_x11=not-run
x11_resources=not-run raster_requests=not-run plane_mask=not-run damage_coalescing=not-run
m4_pixel_regression=not-run m5_policy_regression=not-run malformed_gwipc=not-run
xcb_drawing=not-run final_frame_golden=not-run buffer_release=not-run
compositor_restart=not-run gwm_restart=not-run connection_survival=not-run
replay_hash=not-run post_restart_hash=not-run post_restart_drawing=not-run
rendering_archive=not-run
mkdir -p "$artifact_dir" "$scene_dir" "$dump_dir" "$control_dir"
rm -f "$artifact_dir"/milestone7-* "$facts"; rm -rf "$scene_dir"/* "$dump_dir"/* "$control_dir"/*
touch "$runtime_log" "$meson_log" "$raw_log" "$xcb_log" "$exposure_log" "$release_log" "$restart_log" "$malformed_log"
exec > >(tee -a "$runtime_log") 2>&1
record_facts() {
  status=$?; set +e
  systemctl stop glasswyrmd-m7.service gwm-m7.service gwcomp-m7.service >/dev/null 2>&1
  journalctl -u glasswyrmd-m7.service --no-pager >"$artifact_dir/milestone7-glasswyrmd-journal.log" 2>&1
  journalctl -u gwm-m7.service --no-pager >"$artifact_dir/milestone7-gwm-journal.log" 2>&1
  journalctl -u gwcomp-m7.service --no-pager >"$artifact_dir/milestone7-gwcomp-journal.log" 2>&1
  systemctl reset-failed glasswyrmd-m7.service gwm-m7.service gwcomp-m7.service >/dev/null 2>&1
  rm -rf /run/glasswyrm-m7-gwm /run/glasswyrm-m7-gwcomp /tmp/.X11-unix/X99
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
    if [[ -n "$(portageq match / x11-base/xorg-server 2>/dev/null)" ||
          -n "$(portageq match / x11-base/xwayland 2>/dev/null)" ]]; then
      echo x_servers_absent=false
    else
      echo x_servers_absent=true
    fi
    for key in full_tests sanitizer runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api04_consumers m4_pixel_regression m5_policy_regression m6_metadata_regression raw_little raw_big image_byte_order exposure_events malformed_x11 malformed_gwipc x11_resources raster_requests plane_mask damage_coalescing xcb_drawing final_frame_golden buffer_release compositor_restart gwm_restart connection_survival replay_hash post_restart_hash post_restart_drawing rendering_archive; do printf '%s=%s\n' "$key" "${!key}"; done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT
[[ -f "$source_dir/.glasswyrm-vm-source" ]]
emerge --verbose --noreplace --color=n sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig x11-libs/libxcb x11-base/xcb-proto
rm -rf "$build_dir" "$sanitizer_build_dir" "$runtime_build_dir" "$server_build_dir" "$server_ipc_build_dir" "$gwm_build_dir" "$gwcomp_build_dir" "$ipc_build_dir" "$install_root"
run_build() { name=$1; dir=$2; shift 2; failure_stage="$name"; { meson setup "$dir" "$source_dir" -Dwerror=true "$@"; meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"; }
run_build full-build-and-test "$build_dir"; full_tests=passed
if printf 'int main(void){return 0;}\n' | cc -x c - -fsanitize=address,undefined -o /var/tmp/m7-san-probe && /var/tmp/m7-san-probe; then run_build sanitizer-build-and-test "$sanitizer_build_dir" -Dasan=true -Dubsan=true; sanitizer=passed; else sanitizer=unavailable; fi
run_build integrated-runtime "$runtime_build_dir" -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=true -Dgwcomp=true -Dtools=false; runtime_build=passed
run_build standalone-server "$server_build_dir" -Dlibgwipc=false -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false; server_standalone=passed
run_build server-with-ipc "$server_ipc_build_dir" -Dlibgwipc=true -Dglasswyrmd=true -Dgwm=false -Dgwcomp=false -Dtools=false; server_ipc=passed
run_build gwm-only "$gwm_build_dir" -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false; gwm_only=passed
run_build gwcomp-only "$gwcomp_build_dir" -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false; gwcomp_only=passed
run_build ipc-only "$ipc_build_dir" --prefix=/usr -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false; ipc_only=passed
failure_stage=api-0.4-consumers
DESTDIR="$install_root" meson install -C "$ipc_build_dir" >>"$meson_log" 2>&1
lib="$install_root/usr/lib64"; [[ -d "$lib" ]] || lib="$install_root/usr/lib"
export PKG_CONFIG_PATH="$lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$install_root" LD_LIBRARY_PATH="$lib"
read -r -a flags <<<"$(pkg-config --cflags --libs gwipc)"
for source in gwipc_c_consumer.c gwipc_policy_c_consumer.c gwipc_lifecycle_c_consumer.c; do cc "$source_dir/tests/install/$source" -o "/var/tmp/${source%.c}" "${flags[@]}"; "/var/tmp/${source%.c}"; done
for source in gwipc_cpp_consumer.cpp gwipc_policy_cpp_consumer.cpp gwipc_lifecycle_cpp_consumer.cpp; do c++ -std=c++20 "$source_dir/tests/install/$source" -o "/var/tmp/${source%.cpp}" "${flags[@]}"; "/var/tmp/${source%.cpp}"; done
api04_consumers=passed
meson test -C "$runtime_build_dir" --print-errorlogs gwcomp-golden; m4_pixel_regression=passed
meson test -C "$runtime_build_dir" --print-errorlogs wm-policy; m5_policy_regression=passed
meson test -C "$runtime_build_dir" --print-errorlogs gwipc-malformed 2>&1 | tee -a "$malformed_log"; malformed_gwipc=passed
meson test -C "$runtime_build_dir" --print-errorlogs published-buffer content-presenter gwcomp-metadata-scene 2>&1 | tee -a "$release_log"
meson test -C "$runtime_build_dir" --print-errorlogs drawable-core 2>&1 | tee -a "$meson_log"; damage_coalescing=passed
failure_stage=integrated-three-process-probe
meson test -C "$runtime_build_dir" --print-errorlogs glasswyrmd-integrated-lifecycle 2>&1 | tee -a "$raw_log"
gwm_socket=/run/glasswyrm-m7-gwm/gwm.sock
comp_socket=/run/glasswyrm-m7-gwcomp/gwcomp.sock
rm -rf /run/glasswyrm-m7-gwm /run/glasswyrm-m7-gwcomp /tmp/.X11-unix/X99; mkdir -p /tmp/.X11-unix
unit_properties=(--property=Type=exec --property=Restart=no --property=NoNewPrivileges=yes --property=PrivateDevices=yes --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet= --property=AmbientCapabilities=)
systemd-run --unit=gwm-m7 "${unit_properties[@]}" --property=PrivateTmp=yes --property=RuntimeDirectory=glasswyrm-m7-gwm --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_build_dir/src/gwm" --ipc-socket "$gwm_socket"
systemd-run --unit=gwcomp-m7 "${unit_properties[@]}" --property=PrivateTmp=yes --property=RuntimeDirectory=glasswyrm-m7-gwcomp --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_build_dir/src/gwcomp" --ipc-socket "$comp_socket" --dump-dir "$dump_dir" --scene-manifest "$scene_dir/scene.jsonl"
for socket in "$gwm_socket" "$comp_socket"; do for _ in {1..200}; do [[ -S "$socket" ]] && break; sleep .05; done; [[ -S "$socket" ]]; done
# First prove that the accepted M6 metadata-only launch still emits no pixels.
systemd-run --unit=glasswyrmd-m7 "${unit_properties[@]}" --property=PrivateTmp=no --no-block -- "$runtime_build_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 ]]
"$runtime_build_dir/tests/x11_milestone6_probe" --display :99 --byte-order little >>"$raw_log" 2>&1
[[ ! -e "$dump_dir/frames.jsonl" ]]; m6_metadata_regression=passed
systemctl stop glasswyrmd-m7.service; rm -f /tmp/.X11-unix/X99
systemd-run --unit=glasswyrmd-m7 "${unit_properties[@]}" --property=PrivateTmp=no --no-block -- "$runtime_build_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket" --software-content
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 ]]
"$runtime_build_dir/tests/x11_milestone7_probe" --display :99 --byte-order little --scenario draw >>"$raw_log" 2>&1; raw_little=passed
"$runtime_build_dir/tests/x11_milestone7_probe" --display :99 --byte-order big --scenario draw >>"$raw_log" 2>&1; raw_big=passed image_byte_order=passed
"$runtime_build_dir/tests/x11_milestone7_probe" --display :99 --byte-order little --scenario exposure >>"$exposure_log" 2>&1; exposure_events=passed
"$runtime_build_dir/tests/x11_milestone7_probe" --display :99 --byte-order little --scenario errors >>"$malformed_log" 2>&1; malformed_x11=passed
DISPLAY=:99 XAUTHORITY=/dev/null "$runtime_build_dir/tests/xcb_milestone7_probe" >"$control_dir/xcb-result.json" 2>>"$xcb_log"
cmp "$control_dir/xcb-result.json" "$source_dir/tests/fixtures/m7/xcb-result.json"
cat "$control_dir/xcb-result.json" >>"$xcb_log"
xcb_drawing=passed x11_resources=passed raster_requests=passed
frame_count() { [[ -f "$dump_dir/frames.jsonl" ]] && wc -l <"$dump_dir/frames.jsonl" || printf '0\n'; }
last_frame_field() { tail -n1 "$dump_dir/frames.jsonl" | sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p"; }
frame_field_at() { sed -n "${1}p" "$dump_dir/frames.jsonl" | sed -n "s/.*\"$2\":\"\([^\"]*\)\".*/\1/p"; }
wait_for_frame_after() { previous=$1; for _ in {1..200}; do current=$(frame_count); (( current > previous )) && return 0; sleep .05; done; return 1; }
expected_pre="$(sed -n 's/.*"pre_restart": "\([0-9a-f]*\)".*/\1/p' "$source_dir/tests/fixtures/m7/frame-hashes.json")"
expected_post="$(sed -n 's/.*"post_restart": "\([0-9a-f]*\)".*/\1/p' "$source_dir/tests/fixtures/m7/frame-hashes.json")"
[[ ${#expected_pre} -eq 16 && ${#expected_post} -eq 16 ]]
for _ in {1..200}; do
  release_snapshot="$(journalctl -u glasswyrmd-m7.service --no-pager 2>/dev/null || true)"
  [[ "$release_snapshot" =~ published\ buffer\ released\ buffer=[0-9]+\ reason=1 ]] &&
    [[ "$release_snapshot" =~ published\ buffer\ released\ buffer=[0-9]+\ reason=2 ]] && break
  sleep .05
done
[[ "$release_snapshot" =~ published\ buffer\ released\ buffer=[0-9]+\ reason=1 ]]
[[ "$release_snapshot" =~ published\ buffer\ released\ buffer=[0-9]+\ reason=2 ]]
buffer_release=passed
before_hold=$(frame_count)
"$runtime_build_dir/tests/m7_restart_hold_probe" --display :99 --control-dir "$control_dir" >>"$restart_log" 2>&1 & hold_pid=$!
for _ in {1..200}; do [[ -f "$control_dir/ready" ]] && break; kill -0 "$hold_pid" 2>/dev/null || break; sleep .05; done
[[ -f "$control_dir/ready" ]] || { wait "$hold_pid"; exit 1; }
wait_for_frame_after "$before_hold"
pre_restart_hash="$(last_frame_field fnv1a64)"; [[ "$pre_restart_hash" == "$expected_pre" ]]
pre_restart_count=$(frame_count)
systemctl restart gwcomp-m7.service
for _ in {1..200}; do systemctl is-active --quiet gwcomp-m7.service && [[ -S "$comp_socket" ]] && break; sleep .05; done
systemctl is-active --quiet gwcomp-m7.service; [[ -S "$comp_socket" ]]
wait_for_frame_after "$pre_restart_count"
[[ "$(last_frame_field fnv1a64)" == "$pre_restart_hash" ]]
replay_hash=passed compositor_restart=passed
pre_gwm_count=$(frame_count)
systemctl restart gwm-m7.service
for _ in {1..200}; do systemctl is-active --quiet gwm-m7.service && [[ -S "$gwm_socket" ]] && break; sleep .05; done
systemctl is-active --quiet gwm-m7.service; [[ -S "$gwm_socket" ]]
wait_for_frame_after "$pre_gwm_count"
[[ "$(last_frame_field fnv1a64)" == "$pre_restart_hash" ]]
gwm_restart=passed
pre_continue_count=$(frame_count)
touch "$control_dir/continue"; wait "$hold_pid"
cmp "$restart_result" "$source_dir/tests/fixtures/m7/restart-result.json"
connection_survival=passed plane_mask=passed post_restart_drawing=passed
wait_for_frame_after "$pre_continue_count"
post_restart_frame=$((pre_continue_count + 1))
post_restart_hash_value="$(frame_field_at "$post_restart_frame" fnv1a64)"; [[ "$post_restart_hash_value" == "$expected_post" && "$post_restart_hash_value" != "$pre_restart_hash" ]]
post_restart_hash=passed final_frame_golden=passed
cp "$restart_result" "$artifact_dir/milestone7-restart-result.json"
final_name="$(frame_field_at "$post_restart_frame" file)"; final_ppm="$dump_dir/$final_name"; [[ -n "$final_name" && -s "$final_ppm" ]]
sha256sum "$final_ppm" | tee "$control_dir/final-frame.sha256"
journalctl -u glasswyrmd-m7.service -u gwcomp-m7.service --no-pager >"$release_log"
! grep -Fq 'invalid compositor buffer release' "$release_log"
grep -Fq 'gwcomp: frame accepted' "$release_log"
grep -Eq 'glasswyrmd: published buffer released buffer=[0-9]+ reason=1' "$release_log"
grep -Eq 'glasswyrmd: published buffer released buffer=[0-9]+ reason=2' "$release_log"
cp "$dump_dir/frames.jsonl" "$control_dir/frames.jsonl"; cp "$scene_dir/scene.jsonl" "$control_dir/scene.jsonl"
cp "$final_ppm" "$control_dir/final.ppm"
(cd "$control_dir" && sha256sum final.ppm frames.jsonl scene.jsonl xcb-result.json result.json >SHA256SUMS && tar -cf "$artifact_dir/milestone7-rendering.tar" final.ppm frames.jsonl scene.jsonl xcb-result.json result.json final-frame.sha256 SHA256SUMS)
tar -tf "$artifact_dir/milestone7-rendering.tar" | tee -a "$runtime_log" | grep -Fx final.ppm
rendering_archive=passed
failure_stage=
GUEST_SCRIPT
}

verify_milestone7_source_identity() {
  local status unexpected='' line current
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do [[ -z "$line" || "$line" == '?? Plans/'* ]] || unexpected+="${unexpected:+$'\n'}$line"; done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 7 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M7_TESTED_COMMIT" || "$current" == "$M7_TESTED_COMMIT" ]]
}

prepare_milestone7_evidence() {
  M7_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone7_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M7_REQUIRED_BASE_COMMIT" "$M7_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 7 commit %s\n' "$M7_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone7-*
}

collect_milestone7_artifacts() {
  local name failed=0
  init_artifacts
  for name in "${M7_ARTIFACT_NAMES[@]}"; do guest_run_script 'set -euo pipefail; cat "$1"' "$M7_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1; done
  ssh_arguments
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M7_GUEST_ARTIFACT_DIR/milestone7-rendering.tar" "$ARTIFACTS_PATH_ABS/milestone7-rendering.tar" || failed=1
  [[ ! -f "$ARTIFACTS_PATH_ABS/milestone7-rendering.tar" ]] || tar -tf "$ARTIFACTS_PATH_ABS/milestone7-rendering.tar" >/dev/null || failed=1
  return "$failed"
}

write_milestone7_summary() {
  local requested=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone7-facts.env" out="$ARTIFACTS_PATH_ABS/milestone7-summary.json"
  python3 - "$facts" "$out" "$requested" "$failure" "$M7_REQUIRED_BASE_COMMIT" "${M7_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    k,s,v=line.partition('=')
    if s: facts[k]=v
required='full_tests runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api04_consumers m4_pixel_regression m5_policy_regression m6_metadata_regression raw_little raw_big image_byte_order exposure_events malformed_x11 malformed_gwipc x11_resources raster_requests plane_mask damage_coalescing xcb_drawing final_frame_golden buffer_release compositor_restart gwm_restart connection_survival replay_hash post_restart_hash post_restart_drawing rendering_archive'.split()
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
if facts.get('api_version')!='0.4.0': errors.append('api_version must be 0.4.0')
if facts.get('wire_version')!='1.0' or facts.get('soversion')!='0': errors.append('ABI or wire evidence mismatch')
if facts.get('x_servers_absent')!='true': errors.append('Xorg and Xwayland must be absent')
if facts.get('sanitizer') not in {'passed','unavailable'}: errors.append('sanitizer must be passed or unavailable')
versions={k:facts.get(k,'unknown') for k in ('compiler_c','compiler_cxx','meson_version','ninja_version','systemd_version','xcb_proto')}
errors += [f'{k} must be recorded' for k,v in versions.items() if v in {'','unknown'}]
payload={'required_base_commit':base,'tested_commit':tested,'api_version':facts.get('api_version','unknown'),'soversion':facts.get('soversion','unknown'),'wire_version':facts.get('wire_version','unknown'),'x_servers_absent':facts.get('x_servers_absent','unknown'),'versions':versions,'results':{k:facts.get(k,'unknown') for k in required},'sanitizer':facts.get('sanitizer','unknown'),'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone7_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 script
  require_approval milestone7-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone7_evidence || { status=$?; failure=source-evidence; }
  [[ -n "$failure" ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z "$failure" ]]; then guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p -- "$1"' "$M7_GUEST_ARTIFACT_DIR" || { status=$?; failure=artifact-preparation; }; fi
  if [[ -z "$failure" ]]; then
    if verify_milestone7_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z "$failure" ]]; then script="$(milestone7_guest_script)"; capture_guest_action milestone7-runtime-test "$ARTIFACTS_PATH_ABS/milestone7-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M7_GUEST_ARTIFACT_DIR" || { status=$?; failure=guest-runtime; }; fi
  collect_milestone7_artifacts || collection=$?
  if ((collection)) && [[ -z "$failure" ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -n "$failure" ]]; then write_milestone7_summary false "$failure" || true; printf 'Milestone 7 VM runtime test failed during: %s\n' "$failure" >&2; return "${status:-1}"; fi
  verify_milestone7_source_identity || { write_milestone7_summary false source-identity-changed; return 1; }
  write_milestone7_summary true ''; echo 'Milestone 7 VM runtime test passed.'; print_artifacts
}
