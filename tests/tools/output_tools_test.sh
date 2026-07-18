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

start_server gwinfo-outputs query
outputs=$(${gwinfo} --socket "${socket}" outputs --json)
finish_server
[[ ${outputs} == *'"layout_generation":1'* ]]
[[ ${outputs} == *'"id":"000000000000000b","name":"LEFT"'* ]]
[[ ${outputs} == *'"id":"000000000000000c","name":"RIGHT"'* ]]

start_server gwinfo-windows query
windows=$(${gwinfo} --socket "${socket}" windows --json)
finish_server
[[ ${windows} == *'"window_id":41'* ]]
[[ ${windows} == *'"output_ids":["000000000000000b","000000000000000c"]'* ]]
[[ ${windows} == *'"scale_mode":"scaled-pixmap"'* ]]
[[ ${windows} == *'"focused":true,"fullscreen":true'* ]]

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
