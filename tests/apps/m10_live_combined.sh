#!/bin/sh
set -eu

xeyes=$1
xclock=$2
fixed_time=$3
gwinput=$4
input_socket=$5
control=$6

eyes_pid=
clock_pid=
cleanup() {
  for pid in "$eyes_pid" "$clock_pid"; do
    test -z "$pid" || kill "$pid" 2>/dev/null || true
  done
  for pid in "$eyes_pid" "$clock_pid"; do
    test -z "$pid" || wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT HUP INT TERM

"$xeyes" -version 2>&1 | grep -F '1.3.1' >/dev/null
"$xclock" -version 2>&1 | grep -F '1.2.0' >/dev/null

env LC_ALL=C LANG=C XMODIFIERS=@im=none SESSION_MANAGER= \
  XAUTHORITY=/dev/null DISPLAY=:99 \
  "$xeyes" +shape +render -geometry 150x100+32+32 -fg black -bg white \
  -outline black -center white >"$control/xeyes.out" \
  2>"$control/xeyes.err" &
eyes_pid=$!
sleep 0.1
env LC_ALL=C LANG=C TZ=UTC XMODIFIERS=@im=none SESSION_MANAGER= \
  XAUTHORITY=/dev/null LD_PRELOAD="$fixed_time" DISPLAY=:99 \
  "$xclock" -analog -norender -update 0 -geometry 164x164+240+32 \
  -fg black -bg white -hd black -hl black -fn fixed \
  >"$control/xclock.out" 2>"$control/xclock.err" &
clock_pid=$!

sleep 3
kill -0 "$eyes_pid"
kill -0 "$clock_pid"
printf 'ready\n' >"$control/clients-ready"

post_vt_done=false
while test ! -f "$control/stop-clients"; do
  kill -0 "$eyes_pid"
  kill -0 "$clock_pid"
  if test "$post_vt_done" = false -a -f "$control/post-vt-input"; then
    "$gwinput" --socket "$input_socket" --scenario m10-xeyes-repaint \
      --output "$control/post-vt-input.json"
    post_vt_done=true
    printf 'complete\n' >"$control/post-vt-input-complete"
  fi
  sleep 0.05
done

if test -f "$control/xeyes.err" && grep -F 'X Error' "$control/xeyes.err"; then
  exit 1
fi
if test -f "$control/xclock.err" && grep -F 'X Error' "$control/xclock.err"; then
  exit 1
fi
