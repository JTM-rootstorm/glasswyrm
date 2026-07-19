#!/usr/bin/env bash
set -euo pipefail

if (( $# != 3 )); then
  echo 'usage: output_tools_test.sh FAKE_SERVER GWINFO GWOUT' >&2
  exit 2
fi

server=$1
gwinfo=$2
gwout=$3
directory=$(mktemp -d /tmp/glasswyrm-output-tools-XXXXXX)
server_pid=
trap '[[ -z ${server_pid} ]] || kill "${server_pid}" 2>/dev/null || true; rm -rf "${directory}"' EXIT

start_server() {
  local name=$1
  local mode=$2
  socket=${directory}/${name}.sock
  "${server}" "${socket}" "${mode}" &
  server_pid=$!
  for _ in {1..200}; do
    [[ -S ${socket} ]] && return 0
    kill -0 "${server_pid}" 2>/dev/null || break
    sleep 0.01
  done
  echo "fake output server failed to start for ${name}" >&2
  return 1
}

finish_server() {
  wait "${server_pid}"
  server_pid=
}

expected_outputs='{"layout_generation":1,"root_width":1280,"root_height":480,"primary_output_id":"000000000000000b","outputs":[{"id":"000000000000000b","name":"LEFT","kind":"headless","enabled":true,"connected":true,"primary":true,"physical_width":640,"physical_height":480,"physical_width_mm":0,"physical_height_mm":0,"refresh_millihertz":60000,"logical_x":0,"logical_y":0,"logical_width":640,"logical_height":480,"scale_numerator":1,"scale_denominator":1,"transform":"normal","capabilities":59,"modes":[{"id":"0000000000000015","width":640,"height":480,"refresh_millihertz":60000,"preferred":true,"current":true}]},{"id":"000000000000000c","name":"RIGHT","kind":"headless","enabled":true,"connected":true,"primary":false,"physical_width":640,"physical_height":480,"physical_width_mm":0,"physical_height_mm":0,"refresh_millihertz":60000,"logical_x":640,"logical_y":0,"logical_width":640,"logical_height":480,"scale_numerator":1,"scale_denominator":1,"transform":"normal","capabilities":59,"modes":[{"id":"0000000000000016","width":640,"height":480,"refresh_millihertz":60000,"preferred":true,"current":true}]}]}'
expected_windows='{"layout_generation":1,"windows":[{"window_id":41,"logical_x":600,"logical_y":40,"logical_width":100,"logical_height":80,"primary_output_id":"000000000000000c","output_ids":["000000000000000b","000000000000000c"],"preferred_scale_numerator":5,"preferred_scale_denominator":4,"client_buffer_scale":2,"scale_mode":"scaled-pixmap","visible":true,"focused":true,"fullscreen":true}]}'

start_server gwinfo-outputs query
outputs=$(${gwinfo} --socket "${socket}" outputs --json)
finish_server
[[ ${outputs} == "${expected_outputs}" ]]

start_server gwinfo-windows query
windows=$(${gwinfo} --socket "${socket}" windows --json)
finish_server
[[ ${windows} == "${expected_windows}" ]]

start_server gwout-list query
listed=$(${gwout} --socket "${socket}" list --json)
finish_server
[[ ${listed} == *'"primary_output_id":"000000000000000b"'* ]]
[[ ${listed} == *'"transform":"normal"'* ]]

start_server gwout-set commit
changed=$(${gwout} --socket "${socket}" set RIGHT --position 640,0 \
  --scale 5/4 --json)
finish_server
[[ ${changed} == *'"result":1,"applied_generation":2'* ]]
[[ ${changed} == *'"root_width":1152,"root_height":480'* ]]

start_server gwout-local-validation query
if ${gwout} --socket "${socket}" set RIGHT --scale 5/121 \
    >"${directory}/invalid.out" 2>"${directory}/invalid.err"; then
  echo 'gwout accepted an unsupported exact scale' >&2
  exit 1
fi
finish_server
grep -q 'requested exact scale is unsupported' "${directory}/invalid.err"

start_server gwinfo-vrr vrr-query
vrr=$(${gwinfo} --socket "${socket}" vrr --json)
finish_server
[[ ${vrr} == *'"id":"0x000000000000000b","name":"LEFT","policy":"off","property_present":false,"hardware_capable":false,"kms_controllable":false,"simulated":false,"range_millihertz":null,"decision":"unsupported"'* ]]
[[ ${vrr} == *'"reasons":["output-not-drm","output-not-vrr-capable","vrr-property-missing"]'* ]]
[[ ${vrr} == *'"id":"0x000000000000000c","name":"RIGHT","policy":"off","property_present":false,"hardware_capable":false,"kms_controllable":true,"simulated":true,"range_millihertz":[40000,144000],"decision":"disabled"'* ]]
[[ ${vrr} == *'"reasons":["policy-off","simulated-headless"],"latest_timing_interval_ns":16666667'* ]]
[[ ${vrr} == *'"window":41,"surface":"0x0000000100000029","output":"0x000000000000000c","preference":"prefer","policy_eligible":false,"selected":false,"focused":true,"fullscreen":true,"borderless_fullscreen":false,"exclusive_output_membership":false,"policy_generation":1,"reasons":["window-spans-outputs"]'* ]]

start_server gwinfo-vrr-selector vrr-query
right_vrr=$(${gwinfo} --socket "${socket}" vrr RIGHT --json)
finish_server
[[ ${right_vrr} == *'"name":"RIGHT"'* ]]
[[ ${right_vrr} != *'"name":"LEFT"'* ]]
[[ ${right_vrr} == *'"window":41'* ]]

start_server gwout-vrr-set vrr-commit
changed_vrr=$(${gwout} --socket "${socket}" set RIGHT --vrr fullscreen --json)
finish_server
[[ ${changed_vrr} == *'"result":1,"applied_generation":2'* ]]
[[ ${changed_vrr} == *'"name":"RIGHT","policy":"fullscreen"'* ]]
[[ ${changed_vrr} == *'"decision":"disabled"'* ]]
[[ ${changed_vrr} == *'"reasons":["no-candidate","simulated-headless"]'* ]]
printf '%s' "${changed_vrr}" | python3 -c \
  'import json,sys; value=json.load(sys.stdin); assert set(value) == {"acknowledgement", "state"}'

start_server gwout-vrr-unsupported vrr-query
if ${gwout} --socket "${socket}" set LEFT --vrr focused --json \
    >"${directory}/unsupported.out" 2>"${directory}/unsupported.err"; then
  echo 'gwout enabled VRR on an uncontrollable output' >&2
  exit 1
fi
finish_server
grep -q 'selected output does not provide controllable VRR' \
  "${directory}/unsupported.err"

start_server gwinfo-vrr-duplicate duplicate-vrr
if ${gwinfo} --socket "${socket}" vrr --json \
    >"${directory}/duplicate.out" 2>"${directory}/duplicate.err"; then
  echo 'gwinfo accepted duplicate VRR capability state' >&2
  exit 1
fi
finish_server
grep -q 'VRR snapshot contains duplicate capability state' \
  "${directory}/duplicate.err"
