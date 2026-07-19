#!/usr/bin/env bash
set -euo pipefail

if (( $# != 4 )); then
  echo 'usage: m14_output_tool_fixture_test.sh FAKE_SERVER GWINFO GWOUT FIXTURE_DIR' >&2
  exit 2
fi

server=$1
gwinfo=$2
gwout=$3
fixtures=$4
directory=$(mktemp -d /tmp/glasswyrm-m14-output-tools-XXXXXX)
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

start_server gwinfo-vrr vrr-query
"${gwinfo}" --socket "${socket}" vrr --json >"${directory}/gwinfo-vrr.json"
finish_server
cmp "${fixtures}/gwinfo-vrr.json" "${directory}/gwinfo-vrr.json"

start_server gwout-vrr vrr-commit
"${gwout}" --socket "${socket}" set RIGHT --vrr fullscreen --json \
  >"${directory}/gwout-vrr-result.json"
finish_server
cmp "${fixtures}/gwout-vrr-result.json" "${directory}/gwout-vrr-result.json"
