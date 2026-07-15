#!/usr/bin/env bash
set -euo pipefail
helper=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/m11-xterm-acceptance.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/scenarios"
for name in basic-typing repeat scroll primary-selection clipboard-probe move resize close post-vt post-restart; do
  printf '{"status":"completed","emitted":3}\n' >"$work/scenarios/$name.json"
done
printf 'M11_TYPED M11_TYPED M11_TYPED M11_VT M11_RESTART\n' >"$work/transcript.log"
printf '{"requests":[{"name":"ChangeWindowAttributes"},{"name":"CreateGlyphCursor"},{"name":"FreeCursor"},{"name":"RecolorCursor"},{"name":"GetSelectionOwner"},{"name":"SetSelectionOwner"},{"name":"ConvertSelection"},{"name":"SendEvent"}],"events":[{"event_type":2},{"event_type":2},{"event_type":2},{"event_type":3},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":6},{"event_type":22},{"event_type":28},{"event_type":29},{"event_type":30},{"event_type":31},{"event_type":33}]}\n' >"$work/trace.jsonl"
printf '{"status":"passed","selection":"CLIPBOARD","targets":["TARGETS","UTF8_STRING"],"token":"M11_CLIPBOARD_TOKEN"}\n' >"$work/selection.json"
printf '%s\n' 'move_button=1 resize_button=3 close_keysym=0xffc1 minimum_width=96 minimum_height=64' >"$work/wm.log"
printf '{"frame":1}\n' >"$work/frames.jsonl"
printf '{"cursor":true,"bindings":true,"windows":[{"window_id":1,"x":0,"y":0,"width":100,"height":100},{"window_id":2,"x":10,"y":10,"width":100,"height":100}]}\n{"windows":[{"window_id":1,"x":20,"y":30,"width":120,"height":110}]}\n' >"$work/scene.jsonl"
printf '{"canonical_hash":"abcd","scanout_hash":"abcd"}\n' >"$work/drm.jsonl"
printf 'P6\n1 1\n255\nabc' >"$work/mirror.ppm"
cp "$work/mirror.ppm" "$work/screen.ppm"
"$helper" --xterm-pid $$ --xterm-pid $$ --scenario-dir "$work/scenarios" \
  --transcript "$work/transcript.log" --trace "$work/trace.jsonl" \
  --selection "$work/selection.json" --wm-evidence "$work/wm.log" \
  --frames "$work/frames.jsonl" \
  --scene "$work/scene.jsonl" --drm-report "$work/drm.jsonl" \
  --mirror "$work/mirror.ppm" --screenshot "$work/screen.ppm" \
  --output "$work/result.json"
grep -F '"status": "passed"' "$work/result.json"
printf 'different\n' >"$work/screen.ppm"
if "$helper" --xterm-pid $$ --xterm-pid $$ --scenario-dir "$work/scenarios" \
  --transcript "$work/transcript.log" --trace "$work/trace.jsonl" \
  --selection "$work/selection.json" --wm-evidence "$work/wm.log" \
  --frames "$work/frames.jsonl" \
  --scene "$work/scene.jsonl" --drm-report "$work/drm.jsonl" \
  --mirror "$work/mirror.ppm" --screenshot "$work/screen.ppm" \
  --output "$work/bad.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted mismatched screenshot' >&2
  exit 1
fi
