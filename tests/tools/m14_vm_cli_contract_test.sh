#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
gw_vm=$repo_root/tools/gw-vm
library=$repo_root/tools/gw-vm.d/lib/milestone14.sh
wrapper=$repo_root/tools/gw-vm.d/scenarios/milestone14-runtime-test.sh
temporary=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m14-vm-cli.XXXXXX")
trap 'rm -rf "$temporary"' EXIT

fail() { printf 'm14 VM CLI contract: %s\n' "$*" >&2; exit 1; }
require_text() {
  local file=$1 expected=$2
  grep -F -- "$expected" "$file" >/dev/null || fail "$file lacks: $expected"
}

"$gw_vm" help >"$temporary/help"
require_text "$temporary/help" 'milestone14-runtime-test --yes'

if "$gw_vm" milestone14-runtime-test >"$temporary/gate" 2>&1; then
  fail 'direct M14 command bypassed --yes'
fi
require_text "$temporary/gate" "Action 'milestone14-runtime-test' requires --yes"
if "$wrapper" >"$temporary/wrapper-gate" 2>&1; then
  fail 'M14 scenario wrapper bypassed --yes'
fi
require_text "$temporary/wrapper-gate" "Action 'milestone14-runtime-test' requires --yes"
if "$gw_vm" scenario milestone14-runtime-test >"$temporary/scenario-gate" 2>&1; then
  fail 'named M14 scenario bypassed --yes'
fi
require_text "$temporary/scenario-gate" \
  "Action 'milestone14-runtime-test' requires --yes"
if "$gw_vm" scenario 'milestone14-runtime-test;id' >"$temporary/injection" 2>&1; then
  fail 'scenario-name injection was accepted'
fi
require_text "$temporary/injection" 'Scenario names are fixed'

guest=$(bash -c 'source "$1"; milestone14_guest_script' _ "$library")
cat "$library" >"$temporary/contract"
printf '%s\n' "$guest" >>"$temporary/contract"

headless_stack=$(sed -n '/^start_headless_stack()/,/^}/p' <<<"$guest")
grep -F -- '--game-compat' <<<"$headless_stack" >/dev/null ||
  fail 'M14 headless policy stack does not enable the EWMH game profile'

policy_client=$(sed -n '/^run_policy_client()/,/^}/p' <<<"$guest")
grep -Fx -- '  wait_policy_cleanup' <<<"$policy_client" >/dev/null ||
  fail 'M14 single-client policy transitions do not wait for cleanup'
focus_restore=$(sed -n '/^wait_focus_restore()/,/^}/p' <<<"$guest")
for expected in 'candidate_window' 'client.get("window")' 'x.get("name")=="LEFT"'; do
  grep -F -- "$expected" <<<"$focus_restore" >/dev/null ||
    fail "M14 focus restoration predicate lacks: $expected"
done
focus_handoff=$(sed -n '/wait "$focus_b"; client_pid=0/,/wait "$focus_a_pid"; focus_a_pid=0/p' \
  <<<"$guest")
grep -Fx -- \
  'wait_focus_restore "$work/gwinfo-focus-a.json" "$work/client-focus-a.json"' \
  <<<"$focus_handoff" >/dev/null ||
  fail 'M14 focused-client handoff does not wait for candidate restoration'
if grep -F -- 'sleep .2' <<<"$focus_handoff" >/dev/null; then
  fail 'M14 focused-client handoff still relies on a fixed delay'
fi
focus_cleanup=$(sed -n '/wait "$focus_a_pid"; focus_a_pid=0/,/result\[gwout_vrr\]=passed/p' \
  <<<"$guest")
grep -Fx -- 'wait_policy_cleanup' <<<"$focus_cleanup" >/dev/null ||
  fail 'M14 focused-client transition does not wait for cleanup'

policy_matrix=$(sed -n '/^python3 - "$work\/gwinfo-off.json"/,/result\[vrr_policy_matrix\]=passed/p' \
  <<<"$guest")
app_transitions=$(sed -n '/^transitions=\[\]$/,/^b=left/p' <<<"$policy_matrix")
for expected in \
  "assert 'no-candidate' in out['reasons']" \
  "assert reason in window['reasons']" \
  "'output_reasons':out['reasons']" \
  "'window_reasons':window['reasons']"; do
  grep -F -- "$expected" <<<"$app_transitions" >/dev/null ||
    fail "M14 policy matrix does not preserve output/window reason split: $expected"
done
if grep -F -- "assert reason in out['reasons']" <<<"$app_transitions" >/dev/null; then
  fail 'M14 policy matrix incorrectly expects window rejection reasons on outputs'
fi

sdl_validator=$(sed -n '/^python3 - "$work\/headless-vrr-single.jsonl"/,/result\[sdl_vrr_reuse\]=passed/p' \
  <<<"$guest")
for expected in \
  'enabled_index=next((i for i,x in enumerate(fullscreen)' \
  'assert enabled_index is not None' \
  'assert any(i>enabled_index and not x.get('\''effective_enabled'\'') and' \
  "'NoCandidate' in x.get('reason_names',())" \
  'assert requested and all(not x.get('\''effective_enabled'\'') and'; do
  grep -F -- "$expected" <<<"$sdl_validator" >/dev/null ||
    fail "M14 SDL validator lacks output-decision proof: $expected"
done
for stale in WindowNotFullscreen WindowNotBorderlessFullscreen WindowDidNotRequest; do
  if grep -F -- "$stale" <<<"$sdl_validator" >/dev/null; then
    fail "M14 SDL validator incorrectly expects window reason on output: $stale"
  fi
done

restart_predicate=$(sed -n '/^wait_restart_state()/,/^}/p' <<<"$guest")
for expected in 'state.get("policy")=="focused"' \
  'state.get("decision")=="enabled"' 'state.get("desired_enabled") is True' \
  'state.get("effective_enabled") is True' \
  'state.get("candidate_window")==window' \
  'state.get("reasons")==["simulated-headless"]'; do
  grep -F -- "$expected" <<<"$restart_predicate" >/dev/null ||
    fail "M14 restart-state predicate lacks: $expected"
done
restart_handoff=$(sed -n '/wait_file "$work\/client-restart.json"/,/result\[compositor_replay\]=passed/p' \
  <<<"$guest")
for state in pre-restart post-gwm post-gwcomp; do
  grep -Fx -- \
    "wait_restart_state \"\$work/$state.json\" \"\$work/client-restart.json\"" \
    <<<"$restart_handoff" >/dev/null ||
    fail "M14 restart handoff does not await stable $state state"
done
[[ $(grep -Fc -- 'wait_restart_state ' <<<"$restart_handoff") == 3 ]] ||
  fail 'M14 restart handoff must use exactly three semantic state waits'

qxl_restart=$(sed -n '/^wait_qxl_vrr_off()/,/^chvt 1/p' <<<"$guest")
for expected in \
  'wait_qxl_restart_evidence() {' \
  '_SYSTEMD_INVOCATION_ID=$invocation' \
  "'gwm: policy accepted'" \
  "'gwcomp: frame accepted'" \
  'qxl_gwm_previous_invocation' \
  'qxl_gwcomp_previous_invocation' \
  'systemctl restart gwm-m14-qxl.service' \
  'wait_qxl_restart_evidence gwm-m14-qxl.service' \
  'wait_qxl_vrr_off "$qxl/post-gwm.json"' \
  'systemctl restart gwcomp-m14-qxl.service' \
  'wait_qxl_restart_evidence gwcomp-m14-qxl.service' \
  'wait_qxl_vrr_off "$qxl/post-gwcomp.json"' \
  "value.get('policy')=='off'" \
  "value.get('desired_enabled') is False" \
  "value.get('effective_enabled') is False"; do
  grep -F -- "$expected" <<<"$qxl_restart" >/dev/null ||
    fail "M14 QXL restart proof lacks: $expected"
done
gwm_evidence_line=$(grep -nF -- \
  'wait_qxl_restart_evidence gwm-m14-qxl.service' <<<"$qxl_restart" | cut -d: -f1)
gwm_query_line=$(grep -nF -- 'wait_qxl_vrr_off "$qxl/post-gwm.json"' \
  <<<"$qxl_restart" | cut -d: -f1)
gwcomp_evidence_line=$(grep -nF -- \
  'wait_qxl_restart_evidence gwcomp-m14-qxl.service' <<<"$qxl_restart" | cut -d: -f1)
gwcomp_query_line=$(grep -nF -- 'wait_qxl_vrr_off "$qxl/post-gwcomp.json"' \
  <<<"$qxl_restart" | cut -d: -f1)
((gwm_evidence_line < gwm_query_line &&
  gwcomp_evidence_line < gwcomp_query_line)) ||
  fail 'M14 QXL restart semantic query precedes fresh-process journal proof'

cleanup=$(sed -n '/^cleanup()/,/^}/p' <<<"$guest")
for expected in 'remove_owned_x_socket /tmp/.X11-unix/X98' \
  'remove_owned_x_socket /tmp/.X11-unix/X99' '"$control/input.sock"' \
  'service stop failed:' 'service remained active after cleanup:' \
  'owned sockets remained after cleanup' "'sockets_released'" \
  "'errors':" "'units':units" 'scenario_exit=$final_status' \
  'failure_stage=$saved_stage'; do
  grep -F -- "$expected" <<<"$cleanup" >/dev/null ||
    fail "M14 cleanup lacks: $expected"
done
if grep -E -- 'rm -f( --)? /tmp/\.X11-unix/X(98|99)' <<<"$cleanup" >/dev/null; then
  fail 'M14 cleanup deletes an X socket without ownership verification'
fi
socket_gate=$(sed -n '/! -S \$runtime\/gwm.sock/,/result\[socket_cleanup\]=passed/p' \
  <<<"$guest")
for expected in 'owned_x_socket_released /tmp/.X11-unix/X98' \
  'owned_x_socket_released /tmp/.X11-unix/X99' '! -S $control/input.sock'; do
  grep -F -- "$expected" <<<"$socket_gate" >/dev/null ||
    fail "M14 socket cleanup gate lacks: $expected"
done

service_gate=$(sed -n '/^service_units=(/,/result\[service_results\]=passed/p' \
  <<<"$guest")
for expected in glasswyrmd-m14-qxl.service gwm-m14-qxl.service \
  gwcomp-m14-qxl.service "set(by_id)==expected" \
  "record['SubState']=='dead'" "record['Result']=='success'" \
  "record['ExecMainStatus']=='0'" "uinput=={'Id':'gw-uinput-m14.service'" \
  "'LoadState':'not-found'"; do
  grep -F -- "$expected" <<<"$service_gate" >/dev/null ||
    fail "M14 service-result gate lacks: $expected"
done

for expected in \
  6864ea631d61636289a21c7d2d6655a17be0c004 \
  "snapshot name 'base'" 'snapshot_name=base' \
  'reset; milestone13-runtime-test; reset; milestone14-runtime-test' \
  /var/tmp/glasswyrm-build-m14 /var/tmp/glasswyrm-build-m14-asan \
  /var/tmp/glasswyrm-build-m14-default /var/tmp/glasswyrm-build-m14-headless \
  /var/tmp/glasswyrm-build-m14-drm /var/tmp/glasswyrm-build-m14-clang \
  /var/tmp/glasswyrm-build-m14-components /var/tmp/glasswyrm-m14-headless \
  /var/tmp/glasswyrm-m14-qxl /var/tmp/glasswyrm-m14-control \
  /var/tmp/glasswyrm-m14-clients \
  /var/tmp/glasswyrm-m14-artifacts /run/glasswyrm-m14 \
  '-Dexperimental=false' '-Dexperimental=true' '-Drender_gl=false' \
  '-Drender_gl=true' '-Dasan=true' '-Dubsan=true' \
  server-historical server-m14 compositor-headless-vrr 'setup_build "$drm"' \
  compositor-software compositor-gles api_consumer_versions=0.1-0.9 \
  gwipc_staged_consumers_test.sh source_layout_test.sh \
  drm-vrr-capability-property drm-vrr-saved-state drm-vrr-timing \
  drm-presenter-vrr-state drm-presenter-vrr-integration \
  headless-vrr-simulation headless-vrr-presenter gwcomp-vrr-presentation \
  gwm-vrr-process server-vrr-lifecycle output-tools-control-client \
  gw-vrr-dispatch gw-vrr-integration '"little":true,"big":true' \
  '--headless-output LEFT:800x600@60000' \
  '--headless-output RIGHT:640x480@75000' \
  '--headless-vrr LEFT=40000-60000' '--headless-vrr RIGHT=48000-75000' \
  '--vrr-report' '--vrr-protocol' '--vrr "$mode"' 'gwinfo" --socket' \
  'm14_vrr_client' off fullscreen focused app-requested always-eligible borderless \
  'wait_policy_cleanup' 'coordinated M14 client cleanup' \
  '--preference "$preference"' 'm12/acquire_sdl.sh' 'm12/build_clients.sh' \
  5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 \
  'systemctl restart gwm-m14-headless.service' \
  'systemctl restart gwcomp-m14-headless.service' \
  'M14 QXL unsupported profile requires driver qxl' \
  'gwout: selected output does not provide controllable VRR' \
  '--backend drm' '--drm-api auto' '--connector "$connector"' \
  '--mode 1024x768' '--property="TTYPath=$target_vt"' \
  '--property=KillMode=mixed' '--property=SuccessExitStatus=143' \
  'chvt 1' 'chvt "${target_vt#/dev/tty}"' 'vrr-restore' \
  'milestone14-qxl-capability.json' 'milestone14-qxl-state.json' \
  'milestone14-qxl-drm-report.jsonl' 'milestone14-qxl-vrr-report.jsonl' \
  'milestone14-qxl-kms-before.json' 'milestone14-qxl-kms-after.json' \
  'milestone14-qxl-vt-before.json' 'milestone14-qxl-vt-after.json' \
  'milestone14-qxl-post-vt.json' 'milestone14-qxl-post-gwm.json' \
  'milestone14-qxl-post-gwcomp.json' 'milestone14-qxl-restart.json' \
  'milestone14-qxl-gwm-restart-journal.jsonl' \
  'milestone14-qxl-gwcomp-restart-journal.jsonl' \
  'milestone14-headless-report.jsonl' 'milestone14-gw-vrr.log' \
  'milestone14-gwout.log' 'milestone14-gwinfo.json' \
  'milestone14-policy-matrix.json' 'milestone14-sdl-vrr.json' \
  'milestone14-sdl-probe.json' 'milestone14-client-build.log' \
  'milestone14-restart.json' 'milestone14-restoration.json' \
  'milestone14-service-results.json' 'milestone14-cleanup.json' \
  'milestone14-cleanup.log' \
  'milestone14-glasswyrmd-journal.log' 'milestone14-gwm-journal.log' \
  'milestone14-gwcomp-journal.log' 'milestone14-facts.env' \
  'milestone14-summary.json' 'milestone14-vm-vrr-evidence.tar' \
  'sha256sum -- * >SHA256SUMS' validate_vm_evidence.py \
  'PYTHONDONTWRITEBYTECODE=1 "${command[@]}"'; do
  require_text "$temporary/contract" "$expected"
done

[[ -s $repo_root/tests/compat/m14/vm_evidence.schema.json ]] ||
  fail 'M14 VM evidence schema is missing'

(
  unset GW_VM_MILESTONE14_LOADED
  # shellcheck source=/dev/null
  source "$library"
  milestone14_source_status_ignored \
    '?? tests/compat/m14/__pycache__/validate_vm_evidence.cpython-314.pyc' ||
    fail 'Python bytecode cache should not invalidate M14 source identity'
  milestone14_source_status_ignored \
    '?? tools/__pycache__/gw-hw.cpython-314.pyc' ||
    fail 'top-level tool bytecode should not invalidate M14 source identity'
  if milestone14_source_status_ignored '?? tests/compat/m14/random_probe.cpp'; then
    fail 'untracked implementation source must invalidate M14 source identity'
  fi
)

if grep -Eq '(^|[[:space:]])eval([[:space:]]|$)' "$library"; then
  fail 'M14 harness contains eval'
fi

printf '%s\n' 'm14 VM CLI contract: passed'
