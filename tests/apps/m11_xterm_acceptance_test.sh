#!/usr/bin/env bash
set -euo pipefail
helper=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/m11-xterm-acceptance.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/scenarios"
for name in basic-typing repeat scroll primary-selection clipboard-probe move resize close post-vt post-restart; do
  printf '{"status":"completed","event_count":3}\n' >"$work/scenarios/$name.json"
done
printf 'M11_TYPED\nM11_TYPED\nM11_REPEAT_aaaaaa\nM11_SELECTION_TOKEN\nM11_PASTED_M11_SELECTION_TOKEN\nM11_VT\nM11_RESTART\n' >"$work/transcript.log"
cat >"$work/trace.json" <<'EOF'
{"schema":1,"request_histogram":{"ChangeWindowAttributes":1,"CreateGlyphCursor":1,"FreeCursor":1,"RecolorCursor":1,"GetSelectionOwner":1,"SetSelectionOwner":1,"ConvertSelection":1,"SendEvent":1,"GrabButton":1},"error_histogram":{},"unknown_opcodes":[],"trace_gated_requests":{"GrabButton":1},"event_histogram":{"2":3,"3":1,"4":4,"5":4,"6":1,"22":1,"28":1,"29":1,"30":1,"31":1,"33":1},"event_sequence":[{"client":1,"event_type":2},{"client":1,"event_type":2},{"client":1,"event_type":2},{"client":1,"event_type":3},{"client":1,"event_type":4},{"client":1,"event_type":5},{"client":1,"event_type":4},{"client":1,"event_type":5},{"client":1,"event_type":4},{"client":1,"event_type":5},{"client":1,"event_type":4},{"client":1,"event_type":5},{"client":1,"event_type":6},{"client":1,"event_type":22},{"client":1,"event_type":28},{"client":1,"event_type":29},{"client":1,"event_type":30},{"client":1,"event_type":31},{"client":1,"event_type":33}]}
EOF
printf '{"status":"passed","selection":"CLIPBOARD","targets":["TARGETS","UTF8_STRING"],"token":"M11_CLIPBOARD_TOKEN","primary_replaced":true}\n' >"$work/selection.json"
cat >"$work/geometry.json" <<'EOF'
{"schema":1,"status":"passed","title":"Glasswyrm-M11-B","window_id":4194305,"initial":{"x":384,"y":160,"width":484,"height":316},"moved":{"x":480,"y":224,"width":484,"height":316},"resized":{"x":480,"y":224,"width":544,"height":368},"configure_notify":{"moved":{"x":480,"y":224,"width":484,"height":316},"resized":{"x":480,"y":224,"width":544,"height":368}},"size_hints":{"base_width":4,"base_height":4,"width_increment":6,"height_increment":13},"shell":{"columns":90,"rows":28},"configure_notify_count":2,"resize_expose_count":1}
EOF
cat >"$work/wm.log" <<'EOF'
move_button=1 resize_button=3 close_keysym=0xffc1 minimum_width=96 minimum_height=64
gwm: lifecycle window upsert id=4194305 geometry=10 requested=484x316+480+224 stack=0
gwm: lifecycle window upsert id=4194305 geometry=11 requested=544x368+480+224 stack=0
EOF
cat >"$work/server.log" <<'EOF'
glasswyrmd: cursor publication accepted kind=left-pointer x=0 y=0 visible=1 buffer=attached
glasswyrmd: cursor publication accepted kind=left-pointer x=12 y=14 visible=1 buffer=reused
glasswyrmd: cursor publication accepted kind=xterm-text x=40 y=50 visible=1 buffer=attached
glasswyrmd: cursor publication accepted kind=xterm-text x=44 y=54 visible=1 buffer=reused
glasswyrmd: cursor publication accepted kind=fleur-move x=70 y=80 visible=1 buffer=attached
glasswyrmd: cursor publication accepted kind=bottom-right-resize x=90 y=100 visible=1 buffer=attached
EOF
printf '{"frame":1}\n' >"$work/frames.jsonl"
printf '{"cursor":true,"bindings":true,"windows":[{"window_id":1,"x":0,"y":0,"width":100,"height":100},{"window_id":2,"x":10,"y":10,"width":100,"height":100}]}\n{"windows":[{"window_id":1,"x":20,"y":30,"width":120,"height":110}]}\n' >"$work/scene.jsonl"
printf '{"canonical_hash":"abcd","scanout_hash":"abcd"}\n' >"$work/drm.jsonl"
printf 'P6\n1 1\n255\nabc' >"$work/mirror.ppm"
cp "$work/mirror.ppm" "$work/screen.ppm"

run_acceptance() {
  local result=$1
  "$helper" --xterm-pid $$ --xterm-pid $$ --scenario-dir "$work/scenarios" \
    --transcript "$work/transcript.log" --trace "$work/trace.json" \
    --selection "$work/selection.json" --geometry "$work/geometry.json" \
    --wm-evidence "$work/wm.log" \
    --server-journal "$work/server.log" --frames "$work/frames.jsonl" \
    --scene "$work/scene.jsonl" --drm-report "$work/drm.jsonl" \
    --mirror "$work/mirror.ppm" --screenshot "$work/screen.ppm" \
    --output "$result"
}

run_acceptance "$work/result.json"
grep -F '"status": "passed"' "$work/result.json"
grep -F '"positions": 6' "$work/result.json"
grep -F '"client_message": true' "$work/result.json"
grep -F '"replay_verified": true' "$work/result.json"
grep -F '"ClientMessage": 33' "$work/result.json"
grep -F '"get_geometry": true' "$work/result.json"
grep -F '"shell_dimensions": true' "$work/result.json"
grep -F '"resize_geometry_serial": 11' "$work/result.json"
grep -F '"repeat_characters": 6' "$work/result.json"
cp "$work/trace.json" "$work/trace-good.json"
sed 's/"2":3/"2":4/' "$work/trace-good.json" >"$work/trace.json"
if run_acceptance "$work/bad-event-histogram.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted inconsistent normalized event evidence' >&2
  exit 1
fi
sed 's/"GrabButton":1}/"GrabButton":2}/' "$work/trace-good.json" >"$work/trace.json"
if run_acceptance "$work/bad-trace-gated.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted inconsistent trace-gated evidence' >&2
  exit 1
fi
cp "$work/trace-good.json" "$work/trace.json"
printf 'different\n' >"$work/screen.ppm"
if run_acceptance "$work/bad-screenshot.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted mismatched screenshot' >&2
  exit 1
fi
cp "$work/mirror.ppm" "$work/screen.ppm"

cp "$work/server.log" "$work/server-good.log"
grep -v 'kind=bottom-right-resize' "$work/server-good.log" >"$work/server.log"
if run_acceptance "$work/bad-cursor-kind.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted incomplete cursor kind evidence' >&2
  exit 1
fi
sed 's/buffer=reused/buffer=attached/g' "$work/server-good.log" >"$work/server.log"
if run_acceptance "$work/bad-cursor-reuse.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted cursor evidence without reuse' >&2
  exit 1
fi
cp "$work/server-good.log" "$work/server.log"
sed 's/minimum_width=96/minimum_width=95/' "$work/wm.log" >"$work/wm-bad.log"
mv "$work/wm-bad.log" "$work/wm.log"
if run_acceptance "$work/bad-bindings.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted incorrect GWM binding evidence' >&2
  exit 1
fi
cat >"$work/wm.log" <<'EOF'
move_button=1 resize_button=3 close_keysym=0xffc1 minimum_width=96 minimum_height=64
gwm: lifecycle window upsert id=4194305 geometry=10 requested=484x316+480+224 stack=0
gwm: lifecycle window upsert id=4194305 geometry=11 requested=544x368+480+224 stack=0
EOF
cp "$work/geometry.json" "$work/geometry-good.json"
sed 's/"columns":90/"columns":89/' "$work/geometry-good.json" >"$work/geometry.json"
if run_acceptance "$work/bad-shell-dimensions.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted incorrect shell dimensions' >&2
  exit 1
fi
cp "$work/geometry-good.json" "$work/geometry.json"
sed 's/\"configure_notify\":{\"moved\":{\"x\":480/\"configure_notify\":{\"moved\":{\"x\":479/' \
  "$work/geometry-good.json" >"$work/geometry.json"
if run_acceptance "$work/bad-configure-payload.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted mismatched ConfigureNotify geometry' >&2
  exit 1
fi
cp "$work/geometry-good.json" "$work/geometry.json"
grep -v 'geometry=11' "$work/wm.log" >"$work/wm-bad.log"
mv "$work/wm-bad.log" "$work/wm.log"
if run_acceptance "$work/bad-policy-geometry.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted missing GWM resize geometry' >&2
  exit 1
fi
cat >"$work/wm.log" <<'EOF'
move_button=1 resize_button=3 close_keysym=0xffc1 minimum_width=96 minimum_height=64
gwm: lifecycle window upsert id=4194305 geometry=10 requested=484x316+480+224 stack=0
gwm: lifecycle window upsert id=4194305 geometry=11 requested=544x368+480+224 stack=0
EOF
sed -E 's/x=-?[0-9]+ y=-?[0-9]+/x=0 y=0/g' \
  "$work/server-good.log" >"$work/server.log"
if run_acceptance "$work/bad-cursor-position.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted cursor evidence at one position' >&2
  exit 1
fi
