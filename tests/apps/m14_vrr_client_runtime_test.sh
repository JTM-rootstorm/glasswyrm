#!/usr/bin/env bash
set -euo pipefail

if (( $# != 5 )); then
  printf 'Usage: %s GWM GWCOMP GLASSWYRMD CLIENT VALIDATOR\n' "$0" >&2
  exit 2
fi

gwm=$1
gwcomp=$2
glasswyrmd=$3
client=$4
validator=$5
root=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m14-client-runtime-XXXXXX")
display=
x_socket=
x_socket_inode=
gwm_pid=
gwcomp_pid=
server_pid=
client_pid=

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  for pid in "$client_pid" "$server_pid" "$gwcomp_pid" "$gwm_pid"; do
    if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
    fi
  done
  for pid in "$client_pid" "$server_pid" "$gwcomp_pid" "$gwm_pid"; do
    if [[ -n $pid ]]; then
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [[ -n $x_socket_inode && -S $x_socket ]] &&
      [[ $(stat -c %i "$x_socket") == "$x_socket_inode" ]]; then
    rm -f "$x_socket"
  fi
  if [[ -n $display ]]; then
    rmdir "/tmp/glasswyrm-m14-display-$display.lock" 2>/dev/null || true
  fi
  if (( status != 0 )); then
    for log in "$root"/*.log; do
      [[ -f $log ]] || continue
      printf '\n--- %s ---\n' "${log##*/}" >&2
      sed -n '1,240p' "$log" >&2
    done
  fi
  rm -rf "$root"
  exit "$status"
}
trap cleanup EXIT INT TERM

for ((candidate = 140; candidate <= 199; ++candidate)); do
  if [[ ! -e /tmp/.X11-unix/X$candidate ]] &&
      mkdir "/tmp/glasswyrm-m14-display-$candidate.lock" 2>/dev/null; then
    display=$candidate
    break
  fi
done
[[ -n $display ]] || {
  printf 'No isolated X11 display number was available\n' >&2
  exit 1
}

wait_path() {
  local path=$1
  for ((attempt = 0; attempt < 200; ++attempt)); do
    [[ -S $path ]] && return 0
    sleep .01
  done
  printf 'Timed out waiting for socket: %s\n' "$path" >&2
  return 1
}

"$gwm" --ipc-socket "$root/gwm.sock" >"$root/gwm.log" 2>&1 &
gwm_pid=$!
wait_path "$root/gwm.sock"

"$gwcomp" --backend headless --ipc-socket "$root/gwcomp.sock" \
  --dump-dir "$root/frames" --headless-output 'DP-1:800x600@120000' \
  --headless-vrr 'DP-1=48000-120000' --vrr-report "$root/vrr.jsonl" \
  >"$root/gwcomp.log" 2>&1 &
gwcomp_pid=$!
wait_path "$root/gwcomp.sock"

"$glasswyrmd" --display "$display" --wm-socket "$root/gwm.sock" \
  --compositor-socket "$root/gwcomp.sock" --software-content --output-model \
  --control-socket "$root/control.sock" --game-compat --vrr-protocol \
  >"$root/glasswyrmd.log" 2>&1 &
server_pid=$!
wait_path "$root/control.sock"
x_socket="/tmp/.X11-unix/X$display"
wait_path "$x_socket"
x_socket_inode=$(stat -c %i "$x_socket")

"$client" --display ":$display" --mode cadence \
  --result "$root/client.json" --hold-ms 0 --frames 180 \
  --target-refresh-hz 70 --preference default \
  >"$root/client.log" 2>&1 &
client_pid=$!

for ((attempt = 0; attempt < 600; ++attempt)); do
  if ! kill -0 "$client_pid" 2>/dev/null; then
    break
  fi
  sleep .01
done
if kill -0 "$client_pid" 2>/dev/null; then
  printf 'Cadence client did not finish its bounded startup/run sequence\n' >&2
  exit 1
fi
wait "$client_pid"
client_pid=

"$validator" "$root/client.json"
for pid in "$gwm_pid" "$gwcomp_pid" "$server_pid"; do
  kill -0 "$pid"
done
