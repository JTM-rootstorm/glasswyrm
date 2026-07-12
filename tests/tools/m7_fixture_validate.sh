#!/usr/bin/env bash
set -euo pipefail

fixture_dir=${1:?usage: m7_fixture_validate.sh FIXTURE_DIR}
cd "$fixture_dir"
sha256sum --check SHA256SUMS
grep -Fq '"image_byte_order": "lsb-first"' raw-events.json
grep -Fq '"completed":true' xcb-result.json
grep -Fq '"post_restart_drawing":true' restart-result.json
