#!/usr/bin/env bash
set -euo pipefail
source_dir=${1:?usage: m8_fixture_regenerate.sh CAPTURE_DIR FIXTURE_DIR}
fixture_dir=${2:?usage: m8_fixture_regenerate.sh CAPTURE_DIR FIXTURE_DIR}
required=(raw-little-events.json raw-big-events.json input-acks.json
  xcb-events.json xcb-result.json restart-result.json final.ppm
  frame-hashes.json scene-tail.json)
for name in "${required[@]}"; do
  [[ -f "$source_dir/$name" ]] || { printf 'missing %s\n' "$name" >&2; exit 1; }
  cp -- "$source_dir/$name" "$fixture_dir/$name"
done
(cd "$fixture_dir" && sha256sum "${required[@]}" README.md >SHA256SUMS)
printf 'Review event semantics and final.ppm pixels before committing.\n'
