#!/usr/bin/env bash

if [[ -n "${GW_VM_MILESTONE3_LOADED:-}" ]]; then
  return 0
fi
GW_VM_MILESTONE3_LOADED=1

M3_GUEST_ARTIFACT_DIR="/var/tmp/glasswyrm-m3-artifacts"
M3_REQUIRED_BASE_COMMIT="d6816484f0293b8b47edf0f13e5da691014e3e7c"
M3_TESTED_COMMIT=""
M3_ARTIFACT_NAMES=(
  milestone3-runtime-test.log
  milestone3-meson-test.log
  milestone3-install-test.log
  milestone3-handshake.log
  milestone3-fd-transfer.log
  milestone3-snapshot.log
  milestone3-malformed.log
  milestone3-journal.log
  milestone3-facts.env
)

milestone3_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail

source_dir=$1
artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m3
sanitizer_build_dir=/var/tmp/glasswyrm-build-m3-asan
ipc_build_dir=/var/tmp/glasswyrm-build-m3-ipc-only
install_root=/var/tmp/glasswyrm-m3-install
unit=gwipc-m3.service
socket_path=/run/glasswyrm-m3/gwipc.sock
runtime_log="$artifact_dir/milestone3-runtime-test.log"
meson_log="$artifact_dir/milestone3-meson-test.log"
install_log="$artifact_dir/milestone3-install-test.log"
handshake_log="$artifact_dir/milestone3-handshake.log"
fd_log="$artifact_dir/milestone3-fd-transfer.log"
snapshot_log="$artifact_dir/milestone3-snapshot.log"
malformed_log="$artifact_dir/milestone3-malformed.log"
journal_log="$artifact_dir/milestone3-journal.log"
facts="$artifact_dir/milestone3-facts.env"
failure_stage=dependency-preparation
full_tests_result=not-run
sanitizer_result=not-run
ipc_only_result=not-run
install_layout_result=not-run
c_consumer_result=not-run
cpp_consumer_result=not-run
handshake_result=not-run
ping_pong_result=not-run
contract_roundtrip_result=not-run
fd_transfer_result=not-run
snapshot_result=not-run
version_rejection_result=not-run
role_rejection_result=not-run
capability_rejection_result=not-run
malformed_isolation_result=not-run
sequence_isolation_result=not-run
limit_isolation_result=not-run
unit_result=not-run
unit_invocation_id=

package_installed() {
  [[ -n "$(portageq match / "$1" 2>/dev/null)" ]]
}

mkdir -p "$artifact_dir"
rm -f "$artifact_dir"/milestone3-*.log "$facts"
touch "$runtime_log" "$meson_log" "$install_log" "$handshake_log" \
  "$fd_log" "$snapshot_log" "$malformed_log" "$journal_log"
exec > >(tee -a "$runtime_log") 2>&1

record_facts() {
  local status=$? journal_status=0 api_version=unknown wire_version=unknown soversion=unknown
  set +e
  systemctl stop "$unit" >/dev/null 2>&1
  if [[ -n "$unit_invocation_id" ]]; then
    journalctl "_SYSTEMD_INVOCATION_ID=$unit_invocation_id" --no-pager \
      >"$journal_log" 2>&1 || journal_status=$?
    [[ -s "$journal_log" ]] || journal_status=1
  else
    printf '%s\n' 'systemd invocation ID was not recorded' >"$journal_log"
    journal_status=1
  fi
  if ((status == 0 && journal_status != 0)); then
    status=$journal_status
    failure_stage=journal-collection
  fi
  if [[ -x "$ipc_build_dir/tests/gwipc_wire_probe" ]]; then
    api_version="$($ipc_build_dir/tests/gwipc_wire_probe --print-api-version 2>/dev/null || printf unknown)"
    wire_version="$($ipc_build_dir/tests/gwipc_wire_probe --print-wire-version 2>/dev/null || printf unknown)"
  fi
  if [[ -e "$install_root/usr/lib64/libgwipc.so.0" || -e "$install_root/usr/lib/libgwipc.so.0" ]]; then
    soversion=0
  fi
  {
    printf 'failure_stage=%s\n' "$failure_stage"
    printf 'scenario_exit=%s\n' "$status"
    printf 'compiler_c=%s\n' "$(cc --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'compiler_cxx=%s\n' "$(c++ --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'meson_version=%s\n' "$(meson --version 2>/dev/null || printf unavailable)"
    printf 'ninja_version=%s\n' "$(ninja --version 2>/dev/null || printf unavailable)"
    printf 'systemd_version=%s\n' "$(systemctl --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'api_version=%s\n' "$api_version"
    printf 'soversion=%s\n' "$soversion"
    printf 'wire_version=%s\n' "$wire_version"
    if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then
      printf 'x_servers_absent=false\n'
    else
      printf 'x_servers_absent=true\n'
    fi
    printf 'full_tests=%s\n' "$full_tests_result"
    printf 'sanitizer=%s\n' "$sanitizer_result"
    printf 'ipc_only=%s\n' "$ipc_only_result"
    printf 'install_layout=%s\n' "$install_layout_result"
    printf 'c_consumer=%s\n' "$c_consumer_result"
    printf 'cpp_consumer=%s\n' "$cpp_consumer_result"
    printf 'handshake=%s\n' "$handshake_result"
    printf 'ping_pong=%s\n' "$ping_pong_result"
    printf 'contract_roundtrip=%s\n' "$contract_roundtrip_result"
    printf 'fd_transfer=%s\n' "$fd_transfer_result"
    printf 'snapshot=%s\n' "$snapshot_result"
    printf 'version_rejection=%s\n' "$version_rejection_result"
    printf 'role_rejection=%s\n' "$role_rejection_result"
    printf 'capability_rejection=%s\n' "$capability_rejection_result"
    printf 'malformed_isolation=%s\n' "$malformed_isolation_result"
    printf 'sequence_isolation=%s\n' "$sequence_isolation_result"
    printf 'limit_isolation=%s\n' "$limit_isolation_result"
    printf 'systemd_runtime=%s\n' "$unit_result"
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT

[[ -f "$source_dir/.glasswyrm-vm-source" ]] || {
  echo "Owned source marker is missing: $source_dir/.glasswyrm-vm-source" >&2
  exit 1
}
if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then
  echo 'Milestone 3 requires a guest without Xorg or Xwayland installed' >&2
  exit 1
fi

export NOCOLOR=1
emerge --verbose --noreplace --color=n \
  sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig \
  x11-libs/libxcb x11-base/xcb-proto

failure_stage=full-build-and-test
rm -rf "$build_dir" "$sanitizer_build_dir" "$ipc_build_dir" "$install_root"
{
  meson setup "$build_dir" "$source_dir" -Dwerror=true -Dlibgwipc=true
  meson compile -C "$build_dir"
  meson test -C "$build_dir" --print-errorlogs
} 2>&1 | tee -a "$meson_log"
full_tests_result=passed

failure_stage=sanitizer-build-and-test
sanitizer_probe=/var/tmp/glasswyrm-m3-sanitizer-probe
if printf 'int main(void) { return 0; }\n' | \
    cc -x c - -fsanitize=address,undefined -o "$sanitizer_probe" \
      >>"$meson_log" 2>&1 && "$sanitizer_probe" >>"$meson_log" 2>&1; then
  rm -f "$sanitizer_probe"
  {
    meson setup "$sanitizer_build_dir" "$source_dir" -Dlibgwipc=true -Dasan=true -Dubsan=true
    meson compile -C "$sanitizer_build_dir"
    meson test -C "$sanitizer_build_dir" --print-errorlogs
  } 2>&1 | tee -a "$meson_log"
  sanitizer_result=passed
else
  rm -f "$sanitizer_probe"
  sanitizer_result=unavailable
  echo 'Sanitizer limitation: the guest compiler/runtime cannot execute an ASan+UBSan probe.' | tee -a "$meson_log"
fi

failure_stage=ipc-only-build-and-test
{
  meson setup "$ipc_build_dir" "$source_dir" --prefix=/usr -Dwerror=true -Dlibgwipc=true \
    -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false
  meson compile -C "$ipc_build_dir"
  meson test -C "$ipc_build_dir" --print-errorlogs
} 2>&1 | tee -a "$meson_log"
ipc_only_result=passed

failure_stage=install-and-consumers
DESTDIR="$install_root" meson install -C "$ipc_build_dir" 2>&1 | tee -a "$install_log"
test -e "$install_root/usr/lib64/libgwipc.so.0" || test -e "$install_root/usr/lib/libgwipc.so.0"
test -d "$install_root/usr/include/glasswyrm/ipc"
test -e "$install_root/usr/lib64/pkgconfig/gwipc.pc" || test -e "$install_root/usr/lib/pkgconfig/gwipc.pc"
if find "$install_root" -type f \( -name glasswyrmd -o -name gwm -o -name gwcomp \) | grep -q .; then
  echo 'IPC-only install unexpectedly contains a runtime process' >&2
  exit 1
fi
install_layout_result=passed

pkgconfig_dir="$install_root/usr/lib64/pkgconfig"
lib_dir="$install_root/usr/lib64"
if [[ ! -d "$pkgconfig_dir" ]]; then pkgconfig_dir="$install_root/usr/lib/pkgconfig"; fi
if [[ ! -d "$lib_dir" ]]; then lib_dir="$install_root/usr/lib"; fi
export PKG_CONFIG_PATH="$pkgconfig_dir"
export PKG_CONFIG_SYSROOT_DIR="$install_root"
export LD_LIBRARY_PATH="$lib_dir"
cc "$source_dir/tests/install/gwipc_c_consumer.c" -o /var/tmp/gwipc-c-consumer \
  $(pkg-config --cflags --libs gwipc) 2>&1 | tee -a "$install_log"
/var/tmp/gwipc-c-consumer 2>&1 | tee -a "$install_log"
c_consumer_result=passed
c++ -std=c++20 "$source_dir/tests/install/gwipc_cpp_consumer.cpp" \
  -o /var/tmp/gwipc-cpp-consumer $(pkg-config --cflags --libs gwipc) \
  2>&1 | tee -a "$install_log"
/var/tmp/gwipc-cpp-consumer 2>&1 | tee -a "$install_log"
cpp_consumer_result=passed

server_probe="$ipc_build_dir/tests/gwipc_probe_server"
client_probe="$ipc_build_dir/tests/gwipc_probe_client"
wire_probe="$ipc_build_dir/tests/gwipc_wire_probe"
for executable in "$server_probe" "$client_probe" "$wire_probe"; do
  [[ -x "$executable" ]] || {
    echo "Required Milestone 3 executable is missing: $executable" >&2
    exit 1
  }
done

failure_stage=systemd-runtime
[[ ! -e "$socket_path" ]] || { echo "$socket_path already exists" >&2; exit 1; }
systemctl reset-failed "$unit" >/dev/null 2>&1 || true
systemd-run --unit="$unit" \
  --property=Type=exec \
  --property=NoNewPrivileges=yes \
  --property=PrivateDevices=yes \
  --property=RestrictAddressFamilies=AF_UNIX \
  --property=CapabilityBoundingSet= \
  --property=AmbientCapabilities= \
  --property=Restart=no \
  --property=RuntimeDirectory=glasswyrm-m3 \
  --property=RuntimeDirectoryMode=0700 \
  "$server_probe" --socket "$socket_path"
unit_invocation_id="$(systemctl show "$unit" -p InvocationID --value)"
[[ "$unit_invocation_id" =~ ^[0-9a-f]{32}$ ]] || {
  echo "Unable to record the systemd invocation ID for $unit" >&2
  exit 1
}
for _ in {1..100}; do
  [[ -S "$socket_path" ]] && break
  systemctl is-failed --quiet "$unit" && { echo "$unit failed before creating its socket" >&2; exit 1; }
  sleep 0.1
done
[[ -S "$socket_path" ]] || { echo "Timed out waiting for $socket_path" >&2; exit 1; }
[[ "$(stat -c %a "$socket_path")" == 600 ]]
[[ "$(stat -c %u "$socket_path")" == "$(id -u)" ]]

run_client() {
  local mode=$1 log=$2
  "$client_probe" --socket "$socket_path" --mode "$mode" 2>&1 | tee -a "$log"
}
valid_after_invalid() {
  run_client roundtrip "$handshake_log"
  systemctl is-active --quiet "$unit"
}

failure_stage=handshake-roundtrip
run_client roundtrip "$handshake_log"
handshake_result=passed
failure_stage=ping-pong
run_client ping-pong "$handshake_log"
ping_pong_result=passed
failure_stage=contract-roundtrip
run_client contract-roundtrip "$handshake_log"
contract_roundtrip_result=passed
failure_stage=fd-transfer
run_client fd-transfer "$fd_log"
fd_transfer_result=passed
failure_stage=snapshot
run_client snapshot "$snapshot_log"
snapshot_result=passed

failure_stage=version-rejection
run_client incompatible-version "$malformed_log" && valid_after_invalid
version_rejection_result=passed
failure_stage=role-rejection
run_client wrong-role "$malformed_log" && valid_after_invalid
role_rejection_result=passed
failure_stage=capability-rejection
run_client missing-capability "$malformed_log" && valid_after_invalid
capability_rejection_result=passed
failure_stage=malformed-isolation
run_client malformed-envelope "$malformed_log" && valid_after_invalid
malformed_isolation_result=passed
failure_stage=sequence-isolation
run_client sequence-violation "$malformed_log" && valid_after_invalid
sequence_isolation_result=passed
failure_stage=limit-isolation
run_client limit "$malformed_log" && valid_after_invalid
limit_isolation_result=passed

failure_stage=systemd-shutdown
systemctl is-active --quiet "$unit"
systemctl stop "$unit"
systemctl show "$unit" -p Result --value | grep -Fx success
[[ ! -e "$socket_path" ]] || { echo "$socket_path remained after service shutdown" >&2; exit 1; }
unit_result=passed
failure_stage=
GUEST_SCRIPT
}

collect_milestone3_artifacts() {
  local name path status failed=0
  init_artifacts
  for name in "${M3_ARTIFACT_NAMES[@]}"; do
    path="$ARTIFACTS_PATH_ABS/$name"
    set +e
    guest_run_script 'set -euo pipefail; cat "$1"' "$M3_GUEST_ARTIFACT_DIR/$name" >"$path" 2>&1
    status=$?
    set -e
    if ((status != 0)); then
      failed=1
      printf 'Unable to collect guest artifact: %s\n' "$name" >>"$path"
    fi
  done
  return "$failed"
}

write_milestone3_summary() {
  local passed=$1 failure_stage=${2:-}
  local tested_commit="${M3_TESTED_COMMIT:-unknown}"
  local facts="$ARTIFACTS_PATH_ABS/milestone3-facts.env"
  local summary="$ARTIFACTS_PATH_ABS/milestone3-summary.json"
  require_command python3
  python3 - "$facts" "$summary" "$passed" "$failure_stage" \
    "$M3_REQUIRED_BASE_COMMIT" "$tested_commit" <<'PY'
import json
import pathlib
import sys

facts_path, output_path, passed, failure, base_commit, tested_commit = sys.argv[1:]
facts = {}
path = pathlib.Path(facts_path)
if path.is_file():
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator and key.replace("_", "").isalnum():
            facts[key] = value

errors = []
required = {
    "scenario_exit": "0", "x_servers_absent": "true", "full_tests": "passed",
    "ipc_only": "passed", "install_layout": "passed", "c_consumer": "passed",
    "cpp_consumer": "passed", "handshake": "passed", "ping_pong": "passed",
    "contract_roundtrip": "passed", "fd_transfer": "passed", "snapshot": "passed",
    "version_rejection": "passed", "role_rejection": "passed",
    "capability_rejection": "passed", "malformed_isolation": "passed",
    "sequence_isolation": "passed", "limit_isolation": "passed",
    "systemd_runtime": "passed", "api_version": "0.1.0", "soversion": "0",
    "wire_version": "1.0",
}
for key, expected in required.items():
    if facts.get(key) != expected:
        errors.append(f"{key} must be {expected}")
if facts.get("sanitizer") not in {"passed", "unavailable"}:
    errors.append("sanitizer must be passed or unavailable")
for key in ("compiler_c", "compiler_cxx", "meson_version", "ninja_version", "systemd_version"):
    if not facts.get(key) or facts.get(key) == "unavailable":
        errors.append(f"{key} is missing")
journal = path.with_name("milestone3-journal.log")
if not journal.is_file() or journal.stat().st_size == 0:
    errors.append("current invocation journal is missing or empty")

requested = passed == "true"
effective_failure = failure or facts.get("failure_stage", "")
if requested and errors:
    effective_failure = effective_failure or "evidence-validation"
payload = {
    "required_base_commit": base_commit,
    "tested_commit": tested_commit,
    "guest_versions": {
        "c_compiler": facts.get("compiler_c", "unknown"),
        "cxx_compiler": facts.get("compiler_cxx", "unknown"),
        "meson": facts.get("meson_version", "unknown"),
        "ninja": facts.get("ninja_version", "unknown"),
        "systemd": facts.get("systemd_version", "unknown"),
    },
    "api_version": facts.get("api_version", "unknown"),
    "soversion": facts.get("soversion", "unknown"),
    "wire_version": facts.get("wire_version", "unknown"),
    "xorg_xwayland_absent": facts.get("x_servers_absent") == "true",
    "results": {key: facts.get(key, "unknown") for key in required
                if key not in {"scenario_exit", "x_servers_absent", "api_version", "soversion", "wire_version"}},
    "sanitizer": facts.get("sanitizer", "unknown"),
    "journal": "passed" if journal.is_file() and journal.stat().st_size else "failed",
    "passed": requested and not errors,
    "failure_stage": effective_failure,
    "evidence_errors": errors,
}
pathlib.Path(output_path).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
if requested and errors:
    raise SystemExit(2)
PY
}

verify_milestone3_source_identity() {
  local source_status unexpected_status='' line=''
  source_status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    [[ "$line" == "?? Plans/"* ]] && continue
    unexpected_status+="${unexpected_status:+$'\n'}$line"
  done <<<"$source_status"
  if [[ -n "$unexpected_status" ]]; then
    printf '%s\n%s\n' 'Milestone 3 VM acceptance requires committed source outside Plans/.' \
      "$unexpected_status" >&2
    return 1
  fi
  local current_commit
  current_commit="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  if [[ -n "$M3_TESTED_COMMIT" && "$current_commit" != "$M3_TESTED_COMMIT" ]]; then
    printf '%s\n' 'Milestone 3 source commit changed during acceptance.' >&2
    return 1
  fi
}

prepare_milestone3_evidence() {
  M3_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone3_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M3_REQUIRED_BASE_COMMIT" "$M3_TESTED_COMMIT" || {
      printf 'HEAD is not based on required Milestone 3 commit %s\n' \
        "$M3_REQUIRED_BASE_COMMIT" >&2
      return 1
    }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone3-*.log \
    "$ARTIFACTS_PATH_ABS/milestone3-facts.env" \
    "$ARTIFACTS_PATH_ABS/milestone3-summary.json"
}

clear_milestone3_guest_artifacts() {
  guest_run_script 'set -euo pipefail
artifact_dir=$1
[[ "$artifact_dir" == /var/tmp/glasswyrm-m3-artifacts ]]
rm -rf -- "$artifact_dir"
mkdir -p -- "$artifact_dir"' "$M3_GUEST_ARTIFACT_DIR"
}

milestone3_runtime_test() {
  local approved=$1
  require_approval "milestone3-runtime-test" "$approved"
  require_vm_domain
  init_artifacts
  SCENARIO_RECORDS=()
  local failure='' status=0 collection_status=0 script current_commit
  local artifacts_prepared=false

  if prepare_milestone3_evidence; then :; else status=$?; failure=source-evidence; fi
  if [[ -z "$failure" ]]; then if vm_boot; then :; else status=$?; failure=boot; fi; fi
  if [[ -z "$failure" ]]; then
    if clear_milestone3_guest_artifacts; then artifacts_prepared=true
    else status=$?; failure=artifact-preparation; fi
  fi
  if [[ -z "$failure" ]]; then
    if verify_milestone3_source_identity && push_source && verify_milestone3_source_identity; then :
    else status=$?; failure=push-source; fi
  fi
  if [[ -z "$failure" ]]; then
    script="$(milestone3_guest_script)"
    if capture_guest_action milestone3-runtime-test \
      "$ARTIFACTS_PATH_ABS/milestone3-runtime-test.log" \
      "$script" "$GUEST_SOURCE_PATH" "$M3_GUEST_ARTIFACT_DIR"; then :
    else status=$?; failure=guest-runtime; fi
  fi
  if [[ "$artifacts_prepared" == true ]]; then
    collect_milestone3_artifacts || collection_status=$?
  fi
  if ((collection_status != 0)) && [[ -z "$failure" ]]; then
    status=$collection_status; failure=artifact-collection
  fi
  if [[ -n "$failure" ]]; then
    write_milestone3_summary false "$failure" || return 1
    printf 'Milestone 3 VM runtime test failed during: %s\n' "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  current_commit="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || printf changed)"
  if [[ "$current_commit" != "$M3_TESTED_COMMIT" ]] || ! verify_milestone3_source_identity; then
    write_milestone3_summary false source-identity-changed || return 1
    printf '%s\n' 'Milestone 3 source identity changed during VM acceptance.' >&2
    return 1
  fi
  write_milestone3_summary true "" || return 1
  printf 'Milestone 3 VM runtime test passed.\n'
  print_artifacts
}
