#!/usr/bin/env bash
set -euo pipefail
fixture_dir=${1:?fixture directory is required}
cd "$fixture_dir"
sha256sum -c SHA256SUMS
python3 - structural-events.json xcb-result.schema.json restart-result.json <<'PY'
import json, pathlib, sys
events=json.loads(pathlib.Path(sys.argv[1]).read_text())
assert set(events["vectors"]) == {
  f"{event}_{order}" for event in ("destroy","unmap","map","configure")
  for order in ("little","big")}
for vector in events["vectors"].values():
  assert len(vector) == 64
  bytes.fromhex(vector)
schema=json.loads(pathlib.Path(sys.argv[2]).read_text())
restart=json.loads(pathlib.Path(sys.argv[3]).read_text())
assert schema["additionalProperties"] is False
assert all(restart.values()) and set(restart) == {
  "completed","connection_preserved","focus_preserved","geometry_preserved",
  "tree_preserved","window_preserved"}
PY
