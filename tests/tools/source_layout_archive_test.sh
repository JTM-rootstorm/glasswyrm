#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-source-layout-archive.XXXXXX")
trap 'rm -rf "$temporary"' EXIT

mkdir -p "$temporary/docs/maintenance" "$temporary/tests/tools"
cp -a "$repo_root/src" "$temporary/src"
cp "$repo_root/docs/maintenance/source_size_allowlist.txt" \
  "$temporary/docs/maintenance/source_size_allowlist.txt"
cp "$repo_root/tests/tools/source_layout_test.sh" \
  "$temporary/tests/tools/source_layout_test.sh"

output=$(
  "$temporary/tests/tools/source_layout_test.sh" 2>&1
) || {
  printf '%s\n' "$output" >&2
  exit 1
}

grep -Fq \
  'baseline commit unavailable; enforcing hard and responsibility-specific budgets only' \
  <<<"$output"
grep -Fq 'source-layout: PASS' <<<"$output"

printf '%s\n' 'source-layout archive test passed'
