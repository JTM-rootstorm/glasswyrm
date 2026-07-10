#!/usr/bin/env bash

if [[ -n "${GW_VM_MILESTONE2_LOADED:-}" ]]; then
  return 0
fi
GW_VM_MILESTONE2_LOADED=1

M2_GUEST_ARTIFACT_DIR="/var/tmp/glasswyrm-m2-artifacts"
M2_REQUIRED_BASE_COMMIT="4e219a8093c2b79857efc046c3bf0948cc7704f8"
M2_TESTED_COMMIT=""
M2_ARTIFACT_NAMES=(
  milestone2-runtime-test.log
  milestone2-meson-test.log
  milestone2-raw-probe.log
  milestone2-xcb-probe.log
  milestone2-journal.log
  milestone2-facts.env
)

milestone2_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail

source_dir=$1
artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m2
sanitizer_build_dir=/var/tmp/glasswyrm-build-m2-asan
unit=glasswyrmd-m2.service
runtime_log="$artifact_dir/milestone2-runtime-test.log"
meson_log="$artifact_dir/milestone2-meson-test.log"
raw_log="$artifact_dir/milestone2-raw-probe.log"
xcb_log="$artifact_dir/milestone2-xcb-probe.log"
journal_log="$artifact_dir/milestone2-journal.log"
facts="$artifact_dir/milestone2-facts.env"
failure_stage=dependency-preparation
sanitizer_result=not-run
m1_regression_result=not-run
m2_test_result=not-run
raw_little_result=not-run
raw_big_result=not-run
error_continuation_result=not-run
resource_cleanup_result=not-run
cross_endian_property_result=not-run
xcb_setup_result=not-run
xcb_m2_result=not-run
unit_result=not-run
unit_invocation_id=

package_installed() {
  [[ -n "$(portageq match / "$1" 2>/dev/null)" ]]
}

mkdir -p "$artifact_dir"
rm -f "$artifact_dir"/milestone2-*.log "$facts"
touch "$runtime_log" "$meson_log" "$raw_log" "$xcb_log" "$journal_log"
exec > >(tee -a "$runtime_log") 2>&1

record_facts() {
  local status=$? journal_status=0 xcb_proto_identity
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
  xcb_proto_identity="$(portageq match / x11-base/xcb-proto 2>/dev/null | head -n 1)"
  if [[ -z "$xcb_proto_identity" ]]; then
    xcb_proto_identity="$(find /usr/share/xcb -name xproto.xml -print -quit 2>/dev/null)"
  fi
  {
    printf 'failure_stage=%s\n' "$failure_stage"
    printf 'scenario_exit=%s\n' "$status"
    printf 'compiler_c=%s\n' "$(cc --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'compiler_cxx=%s\n' "$(c++ --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'meson_version=%s\n' "$(meson --version 2>/dev/null || printf unavailable)"
    printf 'ninja_version=%s\n' "$(ninja --version 2>/dev/null || printf unavailable)"
    printf 'systemd_version=%s\n' "$(systemctl --version 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'xcb_proto=%s\n' "${xcb_proto_identity:-unavailable}"
    if package_installed x11-base/xorg-server ||
      package_installed x11-base/xwayland; then
      printf 'x_servers_absent=false\n'
    else
      printf 'x_servers_absent=true\n'
    fi
    printf 'm1_regression_tests=%s\n' "$m1_regression_result"
    printf 'm2_tests=%s\n' "$m2_test_result"
    printf 'sanitizer=%s\n' "$sanitizer_result"
    printf 'raw_little=%s\n' "$raw_little_result"
    printf 'raw_big=%s\n' "$raw_big_result"
    printf 'error_continuation=%s\n' "$error_continuation_result"
    printf 'resource_cleanup=%s\n' "$resource_cleanup_result"
    printf 'cross_endian_property=%s\n' "$cross_endian_property_result"
    printf 'xcb_setup=%s\n' "$xcb_setup_result"
    printf 'xcb_m2=%s\n' "$xcb_m2_result"
    printf 'systemd_runtime=%s\n' "$unit_result"
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT

[[ -f "$source_dir/.glasswyrm-vm-source" ]] || {
  echo "Owned source marker is missing: $source_dir/.glasswyrm-vm-source" >&2
  exit 1
}

if package_installed x11-base/xorg-server ||
  package_installed x11-base/xwayland; then
  echo 'Milestone 2 requires a guest without Xorg or Xwayland installed' >&2
  exit 1
fi

export NOCOLOR=1
emerge --verbose --noreplace --color=n \
  sys-devel/gcc \
  dev-build/meson \
  dev-build/ninja \
  virtual/pkgconfig \
  x11-libs/libxcb \
  x11-base/xcb-proto

failure_stage=meson-build-and-test
rm -rf "$build_dir" "$sanitizer_build_dir"
{
  meson setup "$build_dir" "$source_dir" -Dwerror=true
  meson compile -C "$build_dir"
  meson test -C "$build_dir" --print-errorlogs
} 2>&1 | tee -a "$meson_log"
m1_regression_result=passed
m2_test_result=passed

failure_stage=sanitizer-build-and-test
sanitizer_probe=/var/tmp/glasswyrm-m2-sanitizer-probe
if printf 'int main(void) { return 0; }\n' | \
    cc -x c - -fsanitize=address,undefined -o "$sanitizer_probe" \
      >>"$meson_log" 2>&1 && "$sanitizer_probe" >>"$meson_log" 2>&1; then
  rm -f "$sanitizer_probe"
  {
    meson setup "$sanitizer_build_dir" "$source_dir" -Dasan=true -Dubsan=true
    meson compile -C "$sanitizer_build_dir"
    meson test -C "$sanitizer_build_dir" --print-errorlogs
  } 2>&1 | tee -a "$meson_log"
  sanitizer_result=passed
else
  rm -f "$sanitizer_probe"
  sanitizer_result=unavailable
  echo 'Sanitizer limitation: the guest compiler/runtime cannot execute an ASan+UBSan probe.' | tee -a "$meson_log"
fi

daemon="$build_dir/src/glasswyrmd"
setup_probe="$build_dir/tests/x11_setup_probe"
m2_probe="$build_dir/tests/x11_milestone2_probe"
xcb_setup_probe="$build_dir/tests/xcb_setup_probe"
xcb_m2_probe="$build_dir/tests/xcb_milestone2_probe"
for executable in "$daemon" "$setup_probe" "$m2_probe" "$xcb_setup_probe" "$xcb_m2_probe"; do
  [[ -x "$executable" ]] || {
    echo "Required Milestone 2 executable is missing: $executable" >&2
    exit 1
  }
done

failure_stage=systemd-runtime
[[ ! -e /tmp/.X11-unix/X99 ]] || {
  echo '/tmp/.X11-unix/X99 already exists' >&2
  exit 1
}
systemctl reset-failed "$unit" >/dev/null 2>&1 || true
systemd-run --unit="$unit" \
  --property=Type=exec \
  --property=PrivateTmp=no \
  --property=NoNewPrivileges=yes \
  --property=PrivateDevices=yes \
  --property=RestrictAddressFamilies=AF_UNIX \
  --property=CapabilityBoundingSet= \
  --property=AmbientCapabilities= \
  --property=Restart=no \
  "$daemon" --display 99
unit_invocation_id="$(systemctl show "$unit" -p InvocationID --value)"
[[ "$unit_invocation_id" =~ ^[0-9a-f]{32}$ ]] || {
  echo "Unable to record the systemd invocation ID for $unit" >&2
  exit 1
}

for _ in {1..100}; do
  [[ -S /tmp/.X11-unix/X99 ]] && break
  systemctl is-failed --quiet "$unit" && {
    echo "$unit failed before creating its socket" >&2
    exit 1
  }
  sleep 0.1
done
[[ -S /tmp/.X11-unix/X99 ]] || {
  echo 'Timed out waiting for /tmp/.X11-unix/X99' >&2
  exit 1
}

failure_stage=m1-raw-regression
"$setup_probe" --display :99 --byte-order little 2>&1 | tee -a "$raw_log"
"$setup_probe" --display :99 --byte-order big 2>&1 | tee -a "$raw_log"

failure_stage=m2-raw-little
"$m2_probe" --display :99 --byte-order little --basic 2>&1 | tee -a "$raw_log"
raw_little_result=passed
failure_stage=m2-raw-big
"$m2_probe" --display :99 --byte-order big --basic 2>&1 | tee -a "$raw_log"
raw_big_result=passed
failure_stage=m2-error-continuation
"$m2_probe" --display :99 --errors 2>&1 | tee -a "$raw_log"
error_continuation_result=passed
failure_stage=m2-resource-cleanup
"$m2_probe" --display :99 --cleanup 2>&1 | tee -a "$raw_log"
resource_cleanup_result=passed
failure_stage=m2-cross-endian-property
"$m2_probe" --display :99 --cross-endian 2>&1 | tee -a "$raw_log"
cross_endian_property_result=passed
systemctl is-active --quiet "$unit"

failure_stage=xcb-setup-probe
DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_setup_probe" 2>&1 | tee -a "$xcb_log"
xcb_setup_result=passed
failure_stage=xcb-m2-probe
DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_m2_probe" 2>&1 | tee -a "$xcb_log"
xcb_m2_result=passed
systemctl is-active --quiet "$unit"

failure_stage=systemd-shutdown
systemctl stop "$unit"
systemctl show "$unit" -p Result --value | grep -Fx success
[[ ! -e /tmp/.X11-unix/X99 ]] || {
  echo '/tmp/.X11-unix/X99 remained after service shutdown' >&2
  exit 1
}
unit_result=passed
failure_stage=
GUEST_SCRIPT
}

collect_milestone2_artifacts() {
  local name path status failed=0
  init_artifacts
  for name in "${M2_ARTIFACT_NAMES[@]}"; do
    path="$ARTIFACTS_PATH_ABS/$name"
    set +e
    guest_run_script 'set -euo pipefail; cat "$1"' "$M2_GUEST_ARTIFACT_DIR/$name" >"$path" 2>&1
    status=$?
    set -e
    if ((status != 0)); then
      failed=1
      printf 'Unable to collect guest artifact: %s\n' "$name" >>"$path"
    fi
  done
  return "$failed"
}

write_milestone2_summary() {
  local passed=$1
  local failure_stage=${2:-}
  local tested_commit base_commit facts summary
  tested_commit="${M2_TESTED_COMMIT:-unknown}"
  base_commit="$M2_REQUIRED_BASE_COMMIT"
  facts="$ARTIFACTS_PATH_ABS/milestone2-facts.env"
  summary="$ARTIFACTS_PATH_ABS/milestone2-summary.json"
  require_command python3
  python3 - "$facts" "$summary" "$passed" "$failure_stage" "$base_commit" "$tested_commit" <<'PY'
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

journal_path = path.with_name("milestone2-journal.log")
evidence_errors = []
required_passed = {
    "scenario_exit": "0",
    "m1_regression_tests": "passed",
    "m2_tests": "passed",
    "raw_little": "passed",
    "raw_big": "passed",
    "error_continuation": "passed",
    "resource_cleanup": "passed",
    "cross_endian_property": "passed",
    "xcb_setup": "passed",
    "xcb_m2": "passed",
    "systemd_runtime": "passed",
    "x_servers_absent": "true",
}
for key, expected in required_passed.items():
    if facts.get(key) != expected:
        evidence_errors.append(f"{key} must be {expected}")
if facts.get("sanitizer") not in {"passed", "unavailable"}:
    evidence_errors.append("sanitizer must be passed or unavailable")
for key in (
    "compiler_c",
    "compiler_cxx",
    "meson_version",
    "ninja_version",
    "systemd_version",
    "xcb_proto",
):
    if not facts.get(key) or facts.get(key) == "unavailable":
        evidence_errors.append(f"{key} is missing")
if not journal_path.is_file() or journal_path.stat().st_size == 0:
    evidence_errors.append("current invocation journal is missing or empty")

requested_pass = passed == "true"
evidence_valid = not evidence_errors
effective_failure = failure or facts.get("failure_stage", "")
if requested_pass and not evidence_valid:
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
        "xcb_proto": facts.get("xcb_proto", "unknown"),
    },
    "xorg_xwayland_absent": facts.get("x_servers_absent") == "true",
    "results": {
        "m1_regression_tests": facts.get("m1_regression_tests", "unknown"),
        "m2_tests": facts.get("m2_tests", "unknown"),
        "sanitizer": facts.get("sanitizer", "unknown"),
        "raw_little": facts.get("raw_little", "unknown"),
        "raw_big": facts.get("raw_big", "unknown"),
        "error_continuation": facts.get("error_continuation", "unknown"),
        "resource_cleanup": facts.get("resource_cleanup", "unknown"),
        "cross_endian_property": facts.get("cross_endian_property", "unknown"),
        "xcb_setup": facts.get("xcb_setup", "unknown"),
        "xcb_m2": facts.get("xcb_m2", "unknown"),
        "systemd_runtime": facts.get("systemd_runtime", "unknown"),
    },
    "passed": requested_pass and evidence_valid,
    "failure_stage": effective_failure,
    "evidence_errors": evidence_errors,
}
pathlib.Path(output_path).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
if requested_pass and not evidence_valid:
    raise SystemExit(2)
PY
}

verify_milestone2_source_identity() {
  local source_status unexpected_status='' line=''
  source_status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    if [[ "$line" == "?? Plans/"* ]]; then
      continue
    fi
    unexpected_status+="${unexpected_status:+$'\n'}$line"
  done <<<"$source_status"
  if [[ -n "$unexpected_status" ]]; then
    printf '%s\n' 'Milestone 2 VM acceptance requires committed source outside Plans/.' >&2
    printf '%s\n' "$unexpected_status" >&2
    return 1
  fi
  local current_commit
  current_commit="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  if [[ -n "$M2_TESTED_COMMIT" && "$current_commit" != "$M2_TESTED_COMMIT" ]]; then
    printf '%s\n' 'Milestone 2 source commit changed during acceptance.' >&2
    return 1
  fi
}

prepare_milestone2_evidence() {
  M2_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone2_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor \
    "$M2_REQUIRED_BASE_COMMIT" "$M2_TESTED_COMMIT" || {
      printf 'HEAD is not based on required Milestone 2 commit %s\n' \
        "$M2_REQUIRED_BASE_COMMIT" >&2
      return 1
    }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone2-*.log \
    "$ARTIFACTS_PATH_ABS/milestone2-facts.env" \
    "$ARTIFACTS_PATH_ABS/milestone2-summary.json"
}

clear_milestone2_guest_artifacts() {
  guest_run_script 'set -euo pipefail
artifact_dir=$1
[[ "$artifact_dir" == /var/tmp/glasswyrm-m2-artifacts ]]
rm -rf -- "$artifact_dir"
mkdir -p -- "$artifact_dir"' "$M2_GUEST_ARTIFACT_DIR"
}

milestone2_runtime_test() {
  local approved=$1
  require_approval "milestone2-runtime-test" "$approved"
  require_vm_domain
  init_artifacts
  SCENARIO_RECORDS=()
  local failure='' status=0 collection_status=0 script current_commit
  local artifacts_prepared=false

  if prepare_milestone2_evidence; then :; else status=$?; failure=source-evidence; fi
  if [[ -z "$failure" ]]; then
    if vm_boot; then :; else status=$?; failure=boot; fi
  fi
  if [[ -z "$failure" ]]; then
    if clear_milestone2_guest_artifacts; then
      artifacts_prepared=true
    else
      status=$?
      failure=artifact-preparation
    fi
  fi
  if [[ -z "$failure" ]]; then
    if verify_milestone2_source_identity && push_source &&
      verify_milestone2_source_identity; then
      :
    else
      status=$?
      failure=push-source
    fi
  fi
  if [[ -z "$failure" ]]; then
    script="$(milestone2_guest_script)"
    if capture_guest_action milestone2-runtime-test \
      "$ARTIFACTS_PATH_ABS/milestone2-runtime-test.log" \
      "$script" "$GUEST_SOURCE_PATH" "$M2_GUEST_ARTIFACT_DIR"; then
      :
    else
      status=$?
      failure=guest-runtime
    fi
  fi

  if [[ "$artifacts_prepared" == true ]]; then
    collect_milestone2_artifacts || collection_status=$?
  fi
  if ((collection_status != 0)) && [[ -z "$failure" ]]; then
    status=$collection_status
    failure=artifact-collection
  fi
  if [[ -n "$failure" ]]; then
    write_milestone2_summary false "$failure" || return 1
    printf 'Milestone 2 VM runtime test failed during: %s\n' "$failure" >&2
    print_artifacts >&2
    return "${status:-1}"
  fi
  current_commit="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || printf changed)"
  if [[ "$current_commit" != "$M2_TESTED_COMMIT" ]] ||
    ! verify_milestone2_source_identity; then
    write_milestone2_summary false source-identity-changed || return 1
    printf '%s\n' 'Milestone 2 source identity changed during VM acceptance.' >&2
    return 1
  fi
  write_milestone2_summary true "" || return 1
  printf 'Milestone 2 VM runtime test passed.\n'
  print_artifacts
}
