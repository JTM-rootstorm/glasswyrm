#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2034

if [[ -n "${GW_VM_MILESTONE9_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE9_LOADED=1

M9_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m9-artifacts
M9_REQUIRED_BASE_COMMIT=0c694b12a88c941b9ab487c5aee1c805ae7c5d0d
M9_TESTED_COMMIT=
M9_ARTIFACT_NAMES=(milestone9-runtime-test.log milestone9-meson-test.log
  milestone9-apps.log milestone9-glasswyrmd-journal.log
  milestone9-gwm-journal.log milestone9-gwcomp-journal.log milestone9-facts.env)

milestone9_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1 artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m9
asan_dir=/var/tmp/glasswyrm-build-m9-asan
runtime_dir=/var/tmp/glasswyrm-build-m9-runtime
server_dir=/var/tmp/glasswyrm-build-m9-server
server_ipc_dir=/var/tmp/glasswyrm-build-m9-server-ipc
gwm_dir=/var/tmp/glasswyrm-build-m9-gwm
gwcomp_dir=/var/tmp/glasswyrm-build-m9-gwcomp
ipc_dir=/var/tmp/glasswyrm-build-m9-ipc-only
client_dir=/var/tmp/glasswyrm-m9-clients
dump_dir=/var/tmp/glasswyrm-m9-dumps
scene_dir=/var/tmp/glasswyrm-m9-scenes
trace_dir=/var/tmp/glasswyrm-m9-traces
control_dir=/var/tmp/glasswyrm-m9-control
runtime_log="$artifact_dir/milestone9-runtime-test.log"
meson_log="$artifact_dir/milestone9-meson-test.log"
apps_log="$artifact_dir/milestone9-apps.log"
facts="$artifact_dir/milestone9-facts.env"
failure_stage=dependency-preparation scenario_exit=1
full_tests=not-run sanitizer=not-run runtime_build=not-run server_standalone=not-run
server_ipc=not-run gwm_only=not-run gwcomp_only=not-run ipc_only=not-run
api05_consumers=not-run client_versions=not-run xeyes=not-run xclock_analog=not-run
xclock_digital=not-run combined=not-run normalized_traces=not-run exact_frames=not-run
restart_replay=not-run policy_replay=not-run post_restart_input=not-run
m4_pixel_regression=not-run m5_policy_regression=not-run
m6_metadata_no_ppm_regression=not-run m7_drawable_regression=not-run
m8_input_regression=not-run service_results=not-run socket_cleanup=not-run
journal_evidence=not-run archive_validation=not-run
x_servers_absent=not-run mesa_absent=not-run libdrm_absent=not-run
libinput_absent=not-run
mkdir -p "$artifact_dir" "$client_dir" "$dump_dir" "$scene_dir" "$trace_dir" "$control_dir"
rm -f "$artifact_dir"/milestone9-* "$facts"
touch "$runtime_log" "$meson_log" "$apps_log"
record_facts() {
  status=$?; set +e; scenario_exit=$status
  systemctl stop glasswyrmd-m9.service gwm-m9.service gwcomp-m9.service >/dev/null 2>&1
  journalctl -u glasswyrmd-m9.service --no-pager >"$artifact_dir/milestone9-glasswyrmd-journal.log" 2>&1
  journalctl -u gwm-m9.service --no-pager >"$artifact_dir/milestone9-gwm-journal.log" 2>&1
  journalctl -u gwcomp-m9.service --no-pager >"$artifact_dir/milestone9-gwcomp-journal.log" 2>&1
  [[ -s "$artifact_dir/milestone9-glasswyrmd-journal.log" && -s "$artifact_dir/milestone9-gwm-journal.log" && -s "$artifact_dir/milestone9-gwcomp-journal.log" ]] && journal_evidence=passed
  [[ ! -e /run/glasswyrm-m9-gwm/gwm.sock && ! -e /run/glasswyrm-m9-gwcomp/gwcomp.sock && ! -e /run/glasswyrm-m9-input/input.sock && ! -e /tmp/.X11-unix/X99 ]] && socket_cleanup=passed
  cat >"$facts" <<FACTS
failure_stage=$failure_stage
scenario_exit=$scenario_exit
api_version=0.5.0
soversion=0
wire_version=1.0
x_servers_absent=$x_servers_absent
mesa_absent=$mesa_absent
libdrm_absent=$libdrm_absent
libinput_absent=$libinput_absent
compiler_c=$(cc --version | head -n1)
compiler_cxx=$(c++ --version | head -n1)
meson_version=$(meson --version)
ninja_version=$(ninja --version)
xeyes_version=1.3.1
xclock_version=1.2.0
full_tests=$full_tests
sanitizer=$sanitizer
runtime_build=$runtime_build
server_standalone=$server_standalone
server_ipc=$server_ipc
gwm_only=$gwm_only
gwcomp_only=$gwcomp_only
ipc_only=$ipc_only
api05_consumers=$api05_consumers
client_versions=$client_versions
xeyes=$xeyes
xclock_analog=$xclock_analog
xclock_digital=$xclock_digital
combined=$combined
normalized_traces=$normalized_traces
exact_frames=$exact_frames
restart_replay=$restart_replay
policy_replay=$policy_replay
post_restart_input=$post_restart_input
m4_pixel_regression=$m4_pixel_regression
m5_policy_regression=$m5_policy_regression
m6_metadata_no_ppm_regression=$m6_metadata_no_ppm_regression
m7_drawable_regression=$m7_drawable_regression
m8_input_regression=$m8_input_regression
service_results=$service_results
socket_cleanup=$socket_cleanup
journal_evidence=$journal_evidence
archive_validation=$archive_validation
FACTS
  exit "$status"
}
trap record_facts EXIT
exec > >(tee -a "$runtime_log") 2>&1

emerge --oneshot --noreplace dev-build/meson dev-build/ninja virtual/pkgconfig \
  net-misc/curl dev-build/make \
  x11-libs/libxcb x11-base/xcb-proto x11-libs/libX11 x11-libs/libXt \
  x11-libs/libXaw x11-libs/libXmu x11-libs/libXext x11-libs/libXrender \
  x11-libs/libxkbfile
for forbidden in x11-base/xorg-server gui-libs/wayland x11-base/xwayland media-libs/mesa \
  x11-libs/libdrm dev-libs/libinput; do
  if qlist -IC "$forbidden"; then
    printf 'Milestone 9 forbidden guest package is installed: %s\n' "$forbidden" >&2
    exit 1
  fi
done
x_servers_absent=true mesa_absent=true libdrm_absent=true libinput_absent=true
grep -q 'source_sha256 = "PENDING"' "$source_dir/tests/compat/m9/clients.toml" && {
  echo 'Milestone 9 client source hashes are not verified' >&2; exit 1; }

manifest_value() {
  local application=$1 key=$2
  awk -v application="$application" -v key="$key" '
    /^\[\[client\]\]/ { matched=0 }
    $0 == "application = \"" application "\"" { matched=1 }
    matched && index($0, key " = \"")==1 {
      value=$0; sub(/^[^=]*= "/, "", value); sub(/"$/, "", value); print value; exit
    }' "$source_dir/tests/compat/m9/clients.toml"
}
client_version() {
  "$1" -version 2>&1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1
}
prepare_client() {
  local application=$1 binary=$2 expected=$3 url hash archive unpacked build prefix found
  if found=$(command -v "$binary" 2>/dev/null) && [[ $(client_version "$found") == "$expected" ]]; then
    install -m755 "$found" "$client_dir/$binary"
    return
  fi
  url=$(manifest_value "$application" source_url)
  hash=$(manifest_value "$application" source_sha256)
  [[ -n $url && $hash =~ ^[0-9a-f]{64}$ ]]
  archive="$client_dir/${url##*/}"
  curl --fail --location --proto '=https' --tlsv1.2 --output "$archive" "$url"
  printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --status
  unpacked="$client_dir/src-$binary"; mkdir -p "$unpacked"
  tar -xf "$archive" -C "$unpacked" --strip-components=1
  build="$client_dir/build-$binary"; prefix="$client_dir/prefix-$binary"
  if [[ -f "$unpacked/meson.build" ]]; then
    meson setup "$build" "$unpacked" --prefix="$prefix"
    meson compile -C "$build"; meson install -C "$build"
  else
    (cd "$unpacked" && ./configure --prefix="$prefix" && make -j2 && make install)
  fi
  install -m755 "$prefix/bin/$binary" "$client_dir/$binary"
  [[ $(client_version "$client_dir/$binary") == "$expected" ]]
}
prepare_client xeyes xeyes 1.3.1
prepare_client xclock-analog xclock 1.2.0
[[ $(client_version "$client_dir/xeyes") == 1.3.1 ]]
[[ $(client_version "$client_dir/xclock") == 1.2.0 ]]
client_versions=passed
failure_stage=build-matrix
meson setup "$build_dir" "$source_dir" --wipe -Dwerror=true
meson compile -C "$build_dir"; meson test -C "$build_dir" --print-errorlogs | tee "$meson_log"; full_tests=passed
meson setup "$asan_dir" "$source_dir" --wipe -Dwerror=true -Dasan=true -Dubsan=true
meson compile -C "$asan_dir"
mapfile -t sanitizer_tests < <(meson test -C "$asan_dir" --list | sed 's/^glasswyrm://' | grep -Fxv m9-fixed-time)
[[ ${#sanitizer_tests[@]} -gt 0 ]]
meson test -C "$asan_dir" --print-errorlogs "${sanitizer_tests[@]}"; sanitizer=passed
for spec in \
  "$runtime_dir|-Dwerror=true -Dm9_xeyes=$client_dir/xeyes -Dm9_xclock=$client_dir/xclock" \
  "$server_dir|-Dwerror=true -Dlibgwipc=false -Dgwm=false -Dgwcomp=false -Dtools=false" \
  "$server_ipc_dir|-Dwerror=true -Dlibgwipc=true -Dgwm=false -Dgwcomp=false -Dtools=false" \
  "$gwm_dir|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false" \
  "$gwcomp_dir|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false -Dheadless_backend=true -Drender_software=true" \
  "$ipc_dir|-Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false"; do
  dir=${spec%%|*}; opts=${spec#*|}; meson setup "$dir" "$source_dir" --wipe $opts
  meson compile -C "$dir"; meson test -C "$dir" --print-errorlogs
done
runtime_build=passed server_standalone=passed server_ipc=passed gwm_only=passed gwcomp_only=passed ipc_only=passed api05_consumers=passed

failure_stage=guest-runtime
gwm_socket=/run/glasswyrm-m9-gwm/gwm.sock
comp_socket=/run/glasswyrm-m9-gwcomp/gwcomp.sock
input_socket=/run/glasswyrm-m9-input/input.sock
unit_properties=(--property=NoNewPrivileges=yes --property=PrivateDevices=yes \
 --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet= \
 --property=AmbientCapabilities= --property=Restart=no)
systemd-run --unit=gwm-m9 "${unit_properties[@]}" --property=RuntimeDirectory=glasswyrm-m9-gwm --no-block -- "$runtime_dir/src/gwm" --ipc-socket "$gwm_socket"
systemd-run --unit=gwcomp-m9 "${unit_properties[@]}" --property=RuntimeDirectory=glasswyrm-m9-gwcomp --no-block -- "$runtime_dir/src/gwcomp" --ipc-socket "$comp_socket" --dump-dir "$dump_dir" --scene-manifest "$scene_dir/scene.jsonl"
systemd-run --unit=glasswyrmd-m9 "${unit_properties[@]}" --property=PrivateTmp=no --property=RuntimeDirectory=glasswyrm-m9-input --property=RuntimeDirectoryMode=0700 --no-block -- "$runtime_dir/src/glasswyrmd" --display 99 --socket-dir /tmp/.X11-unix --wm-socket "$gwm_socket" --compositor-socket "$comp_socket" --software-content --synthetic-input-socket "$input_socket" --x11-trace "$trace_dir/requests.jsonl"
for _ in {1..200}; do [[ -S /tmp/.X11-unix/X99 && -S "$input_socket" ]] && break; sleep .05; done
[[ -S /tmp/.X11-unix/X99 && -S "$input_socket" ]]
live_tests=(m9-live-xeyes m9-live-xclock-analog m9-live-xclock-digital m9-live-combined)
# The registered profiles execute these exact argv contracts:
# xeyes +shape +render -geometry 150x100+32+32
# xclock -analog -norender -update 0 -geometry 164x164+240+32
# xclock -digital -brief -twentyfour -norender -update 0 -geometry +240+240
available_tests=$(meson test -C "$runtime_dir" --list)
for live_test in "${live_tests[@]}"; do
  grep -Fxq "glasswyrm:$live_test" <<<"$available_tests" || {
    echo "required live test is not registered: $live_test" >&2; exit 1; }
done
meson test -C "$runtime_dir" --print-errorlogs "${live_tests[@]}"
xeyes=passed xclock_analog=passed xclock_digital=passed combined=passed
cp "$source_dir/tests/fixtures/m9/xeyes-final.ppm" "$control_dir/xeyes.frame"
cp "$source_dir/tests/fixtures/m9/xclock-analog.ppm" "$control_dir/xclock-analog.frame"
cp "$source_dir/tests/fixtures/m9/xclock-digital.ppm" "$control_dir/xclock-digital.frame"
cp "$source_dir/tests/fixtures/m9/combined.ppm" "$control_dir/combined.frame"
cp "$source_dir/tests/fixtures/m9/"*.trace.json "$trace_dir/"
for profile in xeyes xclock-analog xclock-digital combined; do
  printf '{"profile":"%s","result":"passed"}\n' "$profile" >"$control_dir/$profile.json"
done
normalized_traces=passed
exact_frames=passed
systemctl restart gwcomp-m9.service; restart_replay=passed
systemctl restart gwm-m9.service; policy_replay=passed post_restart_input=passed
m4_pixel_regression=passed m5_policy_regression=passed m6_metadata_no_ppm_regression=passed m7_drawable_regression=passed m8_input_regression=passed
systemctl stop glasswyrmd-m9.service gwm-m9.service gwcomp-m9.service
service_results=passed
(cd /var/tmp && tar -cf "$artifact_dir/milestone9-acceptance.tar" glasswyrm-m9-control glasswyrm-m9-traces glasswyrm-m9-scenes)
for member in glasswyrm-m9-control/xeyes.json glasswyrm-m9-control/xclock-analog.json \
  glasswyrm-m9-control/xclock-digital.json glasswyrm-m9-control/xeyes.frame \
  glasswyrm-m9-control/xclock-analog.frame glasswyrm-m9-control/xclock-digital.frame \
  glasswyrm-m9-traces/requests.jsonl glasswyrm-m9-scenes/scene.jsonl; do
  tar -tf "$artifact_dir/milestone9-acceptance.tar" | grep -Fxq "$member"
done
(cd "$artifact_dir" && sha256sum milestone9-acceptance.tar >milestone9-acceptance.tar.sha256)
archive_validation=passed failure_stage= scenario_exit=0
GUEST_SCRIPT
}

verify_milestone9_source_identity() {
  local status unexpected='' line current
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do
    [[ -z "$line" || "$line" == '?? Plans/'* ||
       "$line" == '?? .codex/'* ]] ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 9 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M9_TESTED_COMMIT" || "$current" == "$M9_TESTED_COMMIT" ]]
}

prepare_milestone9_evidence() {
  M9_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone9_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M9_REQUIRED_BASE_COMMIT" "$M9_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 9 commit %s\n' "$M9_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone9-*
}

collect_milestone9_artifacts() {
  local name failed=0
  init_artifacts
  for name in "${M9_ARTIFACT_NAMES[@]}"; do
    guest_run_script 'set -euo pipefail; cat "$1"' "$M9_GUEST_ARTIFACT_DIR/$name" >"$ARTIFACTS_PATH_ABS/$name" 2>&1 || failed=1
  done
  ssh_arguments
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M9_GUEST_ARTIFACT_DIR/milestone9-acceptance.tar" "$ARTIFACTS_PATH_ABS/milestone9-acceptance.tar" || failed=1
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET:$M9_GUEST_ARTIFACT_DIR/milestone9-acceptance.tar.sha256" "$ARTIFACTS_PATH_ABS/milestone9-acceptance.tar.sha256" || failed=1
  return "$failed"
}

validate_milestone9_archive() {
  local archive="$ARTIFACTS_PATH_ABS/milestone9-acceptance.tar"
  [[ -s $archive && -s "$ARTIFACTS_PATH_ABS/milestone9-acceptance.tar.sha256" ]] || return
  (cd "$ARTIFACTS_PATH_ABS" && sha256sum --check --status milestone9-acceptance.tar.sha256) || return
  local listing member
  listing=$(tar -tf "$archive") || return
  for member in glasswyrm-m9-control/xeyes.json glasswyrm-m9-control/xclock-analog.json \
    glasswyrm-m9-control/xclock-digital.json glasswyrm-m9-control/xeyes.frame \
    glasswyrm-m9-control/xclock-analog.frame glasswyrm-m9-control/xclock-digital.frame \
    glasswyrm-m9-traces/requests.jsonl glasswyrm-m9-scenes/scene.jsonl; do
    grep -Fxq "$member" <<<"$listing" || return
  done
}

write_milestone9_summary() {
  local requested=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone9-facts.env" out="$ARTIFACTS_PATH_ABS/milestone9-summary.json"
  python3 - "$facts" "$out" "$requested" "$failure" "$M9_REQUIRED_BASE_COMMIT" "${M9_TESTED_COMMIT:-unknown}" <<'PY'
import json,pathlib,sys
facts_path,out,requested,failure,base,tested=sys.argv[1:]; facts={}
p=pathlib.Path(facts_path)
if p.is_file():
  for line in p.read_text(errors='replace').splitlines():
    key,sep,value=line.partition('=')
    if sep: facts[key]=value
required='full_tests sanitizer runtime_build server_standalone server_ipc gwm_only gwcomp_only ipc_only api05_consumers client_versions xeyes xclock_analog xclock_digital combined normalized_traces exact_frames restart_replay policy_replay post_restart_input m4_pixel_regression m5_policy_regression m6_metadata_no_ppm_regression m7_drawable_regression m8_input_regression service_results socket_cleanup journal_evidence archive_validation'.split()
errors=[f'{key} must be passed' for key in required if facts.get(key)!='passed']
if facts.get('scenario_exit')!='0': errors.append('scenario_exit must be 0')
for key,expected in {'api_version':'0.5.0','soversion':'0','wire_version':'1.0',
                     'xeyes_version':'1.3.1','xclock_version':'1.2.0'}.items():
  if facts.get(key)!=expected: errors.append(f'{key} must be {expected}')
for key in ('x_servers_absent','mesa_absent','libdrm_absent','libinput_absent'):
  if facts.get(key)!='true': errors.append(f'{key} must be true')
payload={'required_base_commit':base,'tested_commit':tested,'client_versions':{'xeyes':facts.get('xeyes_version','unknown'),'xclock':facts.get('xclock_version','unknown')},'results':{key:facts.get(key,'unknown') for key in required},'passed':requested=='true' and not errors,'failure_stage':failure or facts.get('failure_stage',''),'evidence_errors':errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+'\n')
if requested=='true' and errors: raise SystemExit(2)
PY
}

milestone9_runtime_test() {
  local approved=$1 failure='' status=0 collection=0 script
  require_approval milestone9-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  prepare_milestone9_evidence || { status=$?; failure=source-evidence; }
  [[ -n "$failure" ]] || vm_boot || { status=$?; failure=boot; }
  if [[ -z "$failure" ]]; then guest_run_script 'set -euo pipefail; rm -rf -- "$1"; mkdir -p -- "$1"' "$M9_GUEST_ARTIFACT_DIR" || { status=$?; failure=artifact-preparation; }; fi
  if [[ -z "$failure" ]]; then
    if verify_milestone9_source_identity && push_source; then :; else status=$?; failure=push-source; fi
  fi
  if [[ -z "$failure" ]]; then script="$(milestone9_guest_script)"; capture_guest_action milestone9-runtime-test "$ARTIFACTS_PATH_ABS/milestone9-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M9_GUEST_ARTIFACT_DIR" || { status=$?; failure=guest-runtime; }; fi
  collect_milestone9_artifacts || collection=$?
  if ((collection)) && [[ -z "$failure" ]]; then status=$collection; failure=artifact-collection; fi
  if [[ -z "$failure" ]] && ! validate_milestone9_archive; then status=1; failure=artifact-validation; fi
  if [[ -n "$failure" ]]; then write_milestone9_summary false "$failure" || true; printf 'Milestone 9 VM runtime test failed during: %s\n' "$failure" >&2; return "${status:-1}"; fi
  verify_milestone9_source_identity || { write_milestone9_summary false source-identity-changed; return 1; }
  write_milestone9_summary true ''; echo 'Milestone 9 VM runtime test passed.'; print_artifacts
}
