#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n "${GW_VM_MILESTONE6_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE6_LOADED=1

M6_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m6-artifacts
M6_REQUIRED_BASE_COMMIT=9b9170de569fa112c400780beec3140bd4ef6af1
M6_TESTED_COMMIT=
M6_ARTIFACT_NAMES=(milestone6-runtime-test.log milestone6-meson-test.log
  milestone6-raw-x11.log milestone6-scene.log milestone6-restart.json
  milestone6-glasswyrmd-journal.log milestone6-gwm-journal.log
  milestone6-gwcomp-journal.log milestone6-facts.env)

milestone6_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m6
sanitizer_build_dir=/var/tmp/glasswyrm-build-m6-asan
runtime_build_dir=/var/tmp/glasswyrm-build-m6-runtime
server_build_dir=/var/tmp/glasswyrm-build-m6-server
server_ipc_build_dir=/var/tmp/glasswyrm-build-m6-server-ipc
gwm_build_dir=/var/tmp/glasswyrm-build-m6-gwm
gwcomp_build_dir=/var/tmp/glasswyrm-build-m6-gwcomp
ipc_build_dir=/var/tmp/glasswyrm-build-m6-ipc-only
install_root=/var/tmp/glasswyrm-m6-install
scene_dir=/var/tmp/glasswyrm-m6-scenes
runtime_log="$artifact_dir/milestone6-runtime-test.log"
meson_log="$artifact_dir/milestone6-meson-test.log"
raw_log="$artifact_dir/milestone6-raw-x11.log"
scene_log="$artifact_dir/milestone6-scene.log"
facts="$artifact_dir/milestone6-facts.env"
restart_result="$artifact_dir/milestone6-restart.json"
failure_stage=dependency-preparation
full_tests=not-run sanitizer=not-run runtime_build=not-run server_standalone=not-run server_ipc=not-run
gwm_only=not-run gwcomp_only=not-run ipc_only=not-run api04_consumers=not-run
integrated_little_big=not-run no_ppm=not-run scene_archive=not-run
restart_probe=unavailable xcb_m6_probe=unavailable
mkdir -p "$artifact_dir" "$scene_dir"
rm -f "$artifact_dir"/milestone6-* "$facts"; rm -rf "$scene_dir"/*
touch "$runtime_log" "$meson_log" "$raw_log" "$scene_log"
exec > >(tee -a "$runtime_log") 2>&1
record_facts() {
  status=$?; set +e
  systemctl stop glasswyrmd-m6.service gwm-m6.service gwcomp-m6.service >/dev/null 2>&1
  journalctl -u glasswyrmd-m6.service --no-pager >"$artifact_dir/milestone6-glasswyrmd-journal.log" 2>&1
  journalctl -u gwm-m6.service --no-pager >"$artifact_dir/milestone6-gwm-journal.log" 2>&1
  journalctl -u gwcomp-m6.service --no-pager >"$artifact_dir/milestone6-gwcomp-journal.log" 2>&1
  systemctl reset-failed glasswyrmd-m6.service gwm-m6.service gwcomp-m6.service >/dev/null 2>&1
  rm -rf /run/glasswyrm-m6 /tmp/.X11-unix/X99
  api_version=unknown; wire_version=unknown
  if [[ -x "$ipc_build_dir/tests/gwipc_wire_probe" ]]; then
    api_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-api-version 2>/dev/null || printf unknown)"
    wire_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-wire-version 2>/dev/null || printf unknown)"
  fi
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$status"
    printf 'api_version=%s\nsoversion=0\nwire_version=%s\n' "$api_version" "$wire_version"
    if portageq match / x11-base/xorg-server >/dev/null 2>&1 || portageq match / x11-base/xwayland >/dev/null 2>&1; then echo x_servers_absent=false; else echo x_servers_absent=true; fi
    for key in full_tests sanitizer runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api04_consumers integrated_little_big no_ppm scene_archive restart_probe xcb_m6_probe; do printf '%s=%s\n' "$key" "${!key}"; done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT
[[ -f "$source_dir/.glasswyrm-vm-source" ]]
emerge --verbose --noreplace --color=n sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig x11-libs/libxcb x11-base/xcb-proto
rm -rf "$build_dir" "$sanitizer_build_dir" "$runtime_build_dir" "$server_build_dir" "$server_ipc_build_dir" "$gwm_build_dir" "$gwcomp_build_dir" "$ipc_build_dir" "$install_root"
run_build() { name=$1; dir=$2; shift 2; failure_stage="$name"; { meson setup "$dir" "$source_dir" -Dwerror=true "$@"; meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"; }
run_build full-build-and-test "$build_dir"; full_tests=passed
if printf 'int main(void){return 0;}\n' | cc -x c - -fsanitize=address,undefined -o /var/tmp/m6-san-probe && /var/tmp/m6-san-probe; then run_build sanitizer-build-and-test "$sanitizer_build_dir" -Dasan=true -Dubsan=true; sanitizer=passed; else sanitizer=unavailable; fi
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
failure_stage=integrated-three-process-probe
meson test -C "$runtime_build_dir" --print-errorlogs glasswyrmd-integrated-lifecycle 2>&1 | tee -a "$raw_log"
runtime_dir=/run/glasswyrm-m6 control_dir="$runtime_dir/control"
rm -rf "$runtime_dir" /tmp/.X11-unix/X99; mkdir -p "$runtime_dir" "$control_dir" /tmp/.X11-unix
systemd-run --unit=gwm-m6 --property=Type=simple --property=Restart=no --no-block -- "$runtime_build_dir/src/gwm" --ipc-socket "$runtime_dir/gwm.sock"
systemd-run --unit=gwcomp-m6 --property=Type=simple --property=Restart=no --no-block -- "$runtime_build_dir/src/gwcomp" --ipc-socket "$runtime_dir/gwcomp.sock" --dump-dir "$scene_dir"
for socket in "$runtime_dir/gwm.sock" "$runtime_dir/gwcomp.sock"; do for _ in {1..200}; do [[ -S "$socket" ]] && break; sleep .05; done; [[ -S "$socket" ]]; done
systemd-run --unit=glasswyrmd-m6 --property=Type=simple --property=Restart=no --no-block -- "$runtime_build_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$runtime_dir/gwm.sock" --compositor-socket "$runtime_dir/gwcomp.sock"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 ]] && break; sleep .05; done; [[ -S /tmp/.X11-unix/X99 ]]
"$runtime_build_dir/tests/x11_milestone6_probe" --display :99 --byte-order little >>"$raw_log" 2>&1
"$runtime_build_dir/tests/x11_milestone6_probe" --display :99 --byte-order big >>"$raw_log" 2>&1
DISPLAY=:99 "$runtime_build_dir/tests/xcb_milestone6_probe" >>"$raw_log" 2>&1
xcb_m6_probe=passed integrated_little_big=passed
"$runtime_build_dir/tests/m6_restart_hold_probe" --display :99 --control-dir "$control_dir" >>"$raw_log" 2>&1 & hold_pid=$!
for _ in {1..200}; do [[ -f "$control_dir/ready" ]] && break; kill -0 "$hold_pid" 2>/dev/null || break; sleep .05; done
[[ -f "$control_dir/ready" ]] || { wait "$hold_pid"; exit 1; }
systemctl restart gwm-m6.service
for _ in {1..200}; do systemctl is-active --quiet gwm-m6.service && [[ -S "$runtime_dir/gwm.sock" ]] && break; sleep .05; done
systemctl is-active --quiet gwm-m6.service; [[ -S "$runtime_dir/gwm.sock" ]]; sleep .25
systemctl restart gwcomp-m6.service
for _ in {1..200}; do systemctl is-active --quiet gwcomp-m6.service && [[ -S "$runtime_dir/gwcomp.sock" ]] && break; sleep .05; done
systemctl is-active --quiet gwcomp-m6.service; [[ -S "$runtime_dir/gwcomp.sock" ]]; sleep .25
touch "$control_dir/continue"; wait "$hold_pid"
install -m 0644 "$control_dir/result.json" "$restart_result"
grep -F '"completed":true' "$restart_result"; restart_probe=passed
manifest="$(find /tmp -maxdepth 2 -path '/tmp/glasswyrmd-integrated-lifecycle-*/scene.jsonl' -type f -printf '%T@ %p\n' | sort -nr | head -n1 | cut -d' ' -f2-)"
[[ -n "$manifest" && -s "$manifest" ]]; cp "$manifest" "$scene_dir/scene.jsonl"
if find /tmp -maxdepth 2 -path '/tmp/glasswyrmd-integrated-lifecycle-*/*.ppm' -print -quit | grep -q .; then exit 1; fi
no_ppm=passed
(cd "$scene_dir" && sha256sum scene.jsonl >SHA256SUMS && tar -cf "$artifact_dir/milestone6-lifecycle.tar" scene.jsonl SHA256SUMS)
tar -tf "$artifact_dir/milestone6-lifecycle.tar" | tee "$scene_log" | grep -Fx scene.jsonl
scene_archive=passed
failure_stage=
GUEST_SCRIPT
}

verify_milestone6_source_identity() {
  local status unexpected='' line current
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do [[ -z "$line" || "$line" == '?? Plans/'* ]] || unexpected+="${unexpected:+$'\n'}$line"; done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 6 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M6_TESTED_COMMIT" || "$current" == "$M6_TESTED_COMMIT" ]]
}

prepare_milestone6_evidence() {
  M6_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone6_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M6_REQUIRED_BASE_COMMIT" "$M6_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 6 commit %s\n' "$M6_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone6-*
}

collect_milestone6_artifacts() {
  local name failed=0
  init_artifacts
  for name in "${M6_ARTIFACT_NAMES[@]}"; do guest_run_script 'set -euo pipefail; cat "$1"' "$M6_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1; done
  ssh_arguments
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M6_GUEST_ARTIFACT_DIR/milestone6-lifecycle.tar" "$ARTIFACTS_PATH_ABS/milestone6-lifecycle.tar" || failed=1
  [[ ! -f "$ARTIFACTS_PATH_ABS/milestone6-lifecycle.tar" ]] || tar -tf "$ARTIFACTS_PATH_ABS/milestone6-lifecycle.tar" >/dev/null || failed=1
  return "$failed"
}

write_milestone6_summary() {
  local requested=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone6-facts.env" out="$ARTIFACTS_PATH_ABS/milestone6-summary.json"
  python3 - "$facts" "$out" "$requested" "$failure" "$M6_REQUIRED_BASE_COMMIT" "${M6_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    k,s,v=line.partition('=')
    if s: facts[k]=v
required='full_tests runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api04_consumers integrated_little_big no_ppm scene_archive restart_probe xcb_m6_probe'.split()
errors=[f'{k} must be passed' for k in required if facts.get(k)!='passed']
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
if facts.get('api_version')!='0.4.0': errors.append('api_version must be 0.4.0')
if facts.get('wire_version')!='1.0' or facts.get('soversion')!='0': errors.append('ABI or wire evidence mismatch')
if facts.get('x_servers_absent')!='true': errors.append('Xorg and Xwayland must be absent')
if facts.get('sanitizer') not in {'passed','unavailable'}: errors.append('sanitizer must be passed or unavailable')
payload={'required_base_commit':base,'tested_commit':tested,'api_version':facts.get('api_version','unknown'),'results':{k:facts.get(k,'unknown') for k in required},'sanitizer':facts.get('sanitizer','unknown'),'restart_probe':facts.get('restart_probe','unavailable'),'xcb_m6_probe':facts.get('xcb_m6_probe','unavailable'),'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone6_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 script
  require_approval milestone6-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone6_evidence || { status=$?; failure=source-evidence; }
  [[ -n "$failure" ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z "$failure" ]]; then guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p -- "$1"' "$M6_GUEST_ARTIFACT_DIR" || { status=$?; failure=artifact-preparation; }; fi
  if [[ -z "$failure" ]]; then
    if verify_milestone6_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z "$failure" ]]; then script="$(milestone6_guest_script)"; capture_guest_action milestone6-runtime-test "$ARTIFACTS_PATH_ABS/milestone6-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M6_GUEST_ARTIFACT_DIR" || { status=$?; failure=guest-runtime; }; fi
  collect_milestone6_artifacts || collection=$?
  if ((collection)) && [[ -z "$failure" ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -n "$failure" ]]; then write_milestone6_summary false "$failure" || true; printf 'Milestone 6 VM runtime test failed during: %s\n' "$failure" >&2; return "${status:-1}"; fi
  verify_milestone6_source_identity || { write_milestone6_summary false source-identity-changed; return 1; }
  write_milestone6_summary true ''; echo 'Milestone 6 VM runtime test passed.'; print_artifacts
}
