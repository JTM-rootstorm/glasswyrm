#!/bin/sh
set -eu

profile=$1
server=$2
gwm=$3
gwcomp=$4
gwinput=$5
fixed_time=$6
summarizer=$7
fixtures=$8
xeyes=$9
xclock=${10}

case "$profile" in
  xeyes) expected=xeyes-final ;;
  xclock-analog) expected=xclock-analog ;;
  xclock-digital) expected=xclock-digital ;;
  combined) expected=combined ;;
  *) exit 2 ;;
esac

"$xeyes" -version 2>&1 | grep -F '1.3.1' >/dev/null
"$xclock" -version 2>&1 | grep -F '1.2.0' >/dev/null

run=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-m9-live.XXXXXX")
server_pid= wm_pid= comp_pid= eyes_pid= clock_pid=
cleanup() {
  for pid in "$eyes_pid" "$clock_pid" "$server_pid" "$wm_pid" "$comp_pid"; do
    test -z "$pid" || kill "$pid" 2>/dev/null || true
  done
  for pid in "$eyes_pid" "$clock_pid" "$server_pid" "$wm_pid" "$comp_pid"; do
    test -z "$pid" || wait "$pid" 2>/dev/null || true
  done
  rm -rf "$run"
}
trap cleanup EXIT HUP INT TERM
mkdir "$run/dump"

"$gwcomp" --ipc-socket "$run/gwcomp.sock" --dump-dir "$run/dump" \
  --scene-manifest "$run/scene.jsonl" \
  >"$run/gwcomp.out" 2>"$run/gwcomp.err" & comp_pid=$!
"$gwm" --ipc-socket "$run/gwm.sock" >"$run/gwm.out" 2>"$run/gwm.err" & wm_pid=$!
for ignored in $(seq 1 200); do
  test -S "$run/gwcomp.sock" -a -S "$run/gwm.sock" && break
  sleep 0.01
done
test -S "$run/gwcomp.sock" -a -S "$run/gwm.sock"

"$server" --display 93 --socket-dir /tmp/.X11-unix \
  --wm-socket "$run/gwm.sock" --compositor-socket "$run/gwcomp.sock" \
  --software-content --synthetic-input-socket "$run/input.sock" \
  --x11-trace "$run/trace.jsonl" >"$run/server.out" 2>"$run/server.err" &
server_pid=$!
for ignored in $(seq 1 200); do
  test -S /tmp/.X11-unix/X93 -a -S "$run/input.sock" && break
  sleep 0.01
done
test -S /tmp/.X11-unix/X93 -a -S "$run/input.sock"

launch_xeyes() {
  env LC_ALL=C LANG=C XMODIFIERS=@im=none SESSION_MANAGER= \
    XAUTHORITY=/dev/null DISPLAY=:93 \
    "$xeyes" +shape +render -geometry 150x100+32+32 -fg black -bg white \
    -outline black -center white >"$run/xeyes.out" 2>"$run/xeyes.err" &
  eyes_pid=$!
}
launch_analog() {
  env LC_ALL=C LANG=C TZ=UTC XMODIFIERS=@im=none SESSION_MANAGER= \
    XAUTHORITY=/dev/null LD_PRELOAD="$fixed_time" DISPLAY=:93 \
    "$xclock" -analog -norender -update 0 -geometry 164x164+240+32 \
    -fg black -bg white -hd black -hl black -fn fixed \
    >"$run/xclock.out" 2>"$run/xclock.err" &
  clock_pid=$!
}

case "$profile" in
  xeyes)
    launch_xeyes
    sleep 2
    "$gwinput" --socket "$run/input.sock" --scenario m9-xeyes \
      --output "$run/input.json"
    sleep 1
    kill -0 "$eyes_pid"
    ;;
  xclock-analog)
    launch_analog
    sleep 2
    kill -0 "$clock_pid"
    ;;
  xclock-digital)
    env LC_ALL=C LANG=C TZ=UTC XMODIFIERS=@im=none SESSION_MANAGER= \
      XAUTHORITY=/dev/null LD_PRELOAD="$fixed_time" DISPLAY=:93 \
      "$xclock" -digital -brief -twentyfour -norender -update 0 \
      -geometry +240+240 -fg black -bg white -fn fixed \
      >"$run/xclock.out" 2>"$run/xclock.err" & clock_pid=$!
    sleep 2
    kill -0 "$clock_pid"
    ;;
  combined)
    launch_xeyes
    sleep 0.1
    launch_analog
    sleep 3
    kill -0 "$eyes_pid" && kill -0 "$clock_pid"
    ;;
esac

if test -f "$run/xeyes.err"; then ! grep -F 'X Error' "$run/xeyes.err"; fi
if test -f "$run/xclock.err"; then ! grep -F 'X Error' "$run/xclock.err"; fi
frame=$(tail -1 "$run/dump/frames.jsonl" | sed -n 's/.*"file":"\([^"]*\)".*/\1/p')
test -n "$frame"
if ! cmp "$run/dump/$frame" "$fixtures/$expected.ppm"; then
  sha256sum "$run/dump/$frame" "$fixtures/$expected.ppm" >&2
  exit 1
fi
"$summarizer" "$run/trace.jsonl" >"$run/trace.json"
if ! cmp "$run/trace.json" "$fixtures/$profile.trace.json"; then
  diff -u "$fixtures/$profile.trace.json" "$run/trace.json" >&2 || true
  exit 1
fi
if test -n "${GW_M9_EVIDENCE_DIR-}"; then
  profile_evidence=$GW_M9_EVIDENCE_DIR/$profile
  mkdir -p "$profile_evidence"
  cp "$run/dump/$frame" "$profile_evidence/final.ppm"
  cp "$run/dump/frames.jsonl" "$profile_evidence/frames.jsonl"
  cp "$run/scene.jsonl" "$profile_evidence/scene.jsonl"
  cp "$run/trace.jsonl" "$profile_evidence/requests.jsonl"
  cp "$run/trace.json" "$profile_evidence/trace.json"
fi
