#!/usr/bin/env bash

write_summary() {
  local passed="$1"
  local failing_scenario="${2:-}"
  local records_file summary_tmp commit
  init_artifacts
  require_command python3
  records_file="$(mktemp "$ARTIFACTS_PATH_ABS/.scenarios.XXXXXX")" || return
  summary_tmp="$ARTIFACTS_PATH_ABS/.summary.json.tmp"
  printf '%s\n' "${SCENARIO_RECORDS[@]}" >"$records_file" || return
  commit="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || printf unknown)"

  local python_status
  if python3 - "$records_file" "$summary_tmp" "$passed" "$failing_scenario" \
    "$commit" "$VM_DOMAIN" "$LIBVIRT_URI" "$OVERLAY_PATH_DISPLAY" <<'PY'
import datetime
import json
import pathlib
import sys

records_path, output_path, passed, failure, commit, domain, uri, overlay = sys.argv[1:]
scenarios = []
for raw in pathlib.Path(records_path).read_text(encoding="utf-8").splitlines():
    if not raw:
        continue
    name, status, log = (raw.split("\t", 2) + ["", ""])[:3]
    item = {"name": name, "status": status}
    if log:
        item["log"] = log
    scenarios.append(item)

summary = {
    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z"),
    "commit": commit,
    "vm_domain": domain,
    "libvirt_uri": uri,
    "overlay_path": overlay,
    "scenarios": scenarios,
    "passed": passed == "true",
}
if failure:
    summary["failed_scenario"] = failure
pathlib.Path(output_path).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY
  then
    python_status=0
  else
    python_status=$?
  fi
  if ((python_status != 0)); then
    rm -f "$records_file" "$summary_tmp"
    return "$python_status"
  fi
  mv "$summary_tmp" "$ARTIFACTS_PATH_ABS/summary.json" || return
  rm -f "$records_file"
}

collect_one() {
  local name="$1"
  local output="$2"
  local script="$3"
  local status
  set +e
  guest_run_script "$script" >"$output" 2>&1
  status=$?
  set -e
  if ((status == 0)); then
    record_scenario "collect-$name" "passed" "$output"
  else
    record_scenario "collect-$name" "failed" "$output"
  fi
  return "$status"
}

collect_logs() {
  init_artifacts
  local failed=0
  local emerge_info profile_list installed emerge_tail elog_tail
  emerge_info='set -o pipefail; emerge --info'
  profile_list='set -o pipefail; eselect profile list'
  installed='set -o pipefail; if command -v qlist >/dev/null 2>&1; then qlist -Iv | sort; else echo "qlist is not installed; install app-portage/portage-utils for the package inventory"; fi'
  emerge_tail='set -o pipefail; if [[ -f /var/log/emerge.log ]]; then tail -n 500 /var/log/emerge.log; else echo "/var/log/emerge.log is absent"; fi'
  elog_tail='set -o pipefail; if [[ -f /var/log/portage/elog/summary.log ]]; then tail -n 500 /var/log/portage/elog/summary.log; else echo "/var/log/portage/elog/summary.log is absent"; fi'

  collect_one emerge-info "$ARTIFACTS_PATH_ABS/emerge-info.txt" "$emerge_info" || failed=1
  collect_one profiles "$ARTIFACTS_PATH_ABS/profiles.txt" "$profile_list" || failed=1
  collect_one installed-packages "$ARTIFACTS_PATH_ABS/installed-packages.txt" "$installed" || failed=1
  collect_one emerge-log-tail "$ARTIFACTS_PATH_ABS/emerge-log-tail.txt" "$emerge_tail" || failed=1
  collect_one portage-elog-tail "$ARTIFACTS_PATH_ABS/portage-elog-tail.txt" "$elog_tail" || failed=1

  if ((failed == 0)); then
    record_scenario "collect" "passed" ""
    write_summary true "" || return
    return 0
  fi
  record_scenario "collect" "failed" ""
  write_summary false "collect" || return
  return 1
}

write_narrow_report() {
  local passed="$1"
  local allow_rebuild="$2"
  shift 2
  local report="$ARTIFACTS_PATH_ABS/narrow-test-gwm.json"
  require_command python3
  python3 - "$report" "$passed" "$allow_rebuild" "$@" <<'PY'
import json
import pathlib
import sys

report, passed, allowed, *rebuilds = sys.argv[1:]
payload = {
    "scenario": "narrow-test-gwm",
    "package": "x11-wm/gwm",
    "unexpected_rebuilds": [] if allowed == "true" else rebuilds,
    "allowed_rebuilds": rebuilds if allowed == "true" else [],
    "passed": passed == "true",
}
pathlib.Path(report).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY
}
