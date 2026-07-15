#!/usr/bin/env bash
set -euo pipefail
helper=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/m11-xterm-acceptance.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/scenarios"
for name in basic-typing repeat scroll primary-selection clipboard-probe move resize close post-vt post-restart; do
  printf '{"status":"completed","emitted":3}\n' >"$work/scenarios/$name.json"
done
printf 'M11_TYPED M11_TYPED M11_SELECTION_TOKEN M11_SELECTION_TOKEN M11_VT M11_RESTART\n' >"$work/transcript.log"
printf '{"requests":[{"name":"ChangeWindowAttributes"},{"name":"CreateGlyphCursor"},{"name":"FreeCursor"},{"name":"RecolorCursor"},{"name":"GetSelectionOwner"},{"name":"SetSelectionOwner"},{"name":"ConvertSelection"},{"name":"SendEvent"}],"events":[{"event_type":2},{"event_type":2},{"event_type":2},{"event_type":3},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":4},{"event_type":5},{"event_type":6},{"event_type":22},{"event_type":28},{"event_type":29},{"event_type":30},{"event_type":31},{"event_type":33}]}\n' >"$work/trace.jsonl"
printf '{"status":"passed","selection":"CLIPBOARD","targets":["TARGETS","UTF8_STRING"],"token":"M11_CLIPBOARD_TOKEN"}\n' >"$work/selection.json"
printf '%s\n' 'move_button=1 resize_button=3 close_keysym=0xffc1 minimum_width=96 minimum_height=64' >"$work/wm.log"
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
    --transcript "$work/transcript.log" --trace "$work/trace.jsonl" \
    --selection "$work/selection.json" --wm-evidence "$work/wm.log" \
    --server-journal "$work/server.log" --frames "$work/frames.jsonl" \
    --scene "$work/scene.jsonl" --drm-report "$work/drm.jsonl" \
    --mirror "$work/mirror.ppm" --screenshot "$work/screen.ppm" \
    --output "$result"
}

run_acceptance "$work/result.json"
grep -F '"status": "passed"' "$work/result.json"
grep -F '"positions": 6' "$work/result.json"
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
sed -E 's/x=-?[0-9]+ y=-?[0-9]+/x=0 y=0/g' \
  "$work/server-good.log" >"$work/server.log"
if run_acceptance "$work/bad-cursor-position.json" >/dev/null 2>&1; then
  printf '%s\n' 'acceptance helper accepted cursor evidence at one position' >&2
  exit 1
fi
