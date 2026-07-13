#!/usr/bin/env bash
set -euo pipefail
fixture_dir=${1:?usage: m8_fixture_validate.sh FIXTURE_DIR}
cd "$fixture_dir"
sha256sum --check SHA256SUMS
[[ -s final.ppm ]]
grep -Fq '"byte_order":"little"' raw-little-events.json
grep -Fq '"byte_order":"big"' raw-big-events.json
grep -Fq '"completed":true' xcb-result.json
grep -Fq '"same_input_connection":true' restart-result.json
grep -Fq '"post_restart_input":true' restart-result.json
grep -Eq '"xcb_final":"[0-9a-f]{16}"' frame-hashes.json
grep -Eq '"pre_restart":"[0-9a-f]{16}"' frame-hashes.json
grep -Eq '"post_restart":"[0-9a-f]{16}"' frame-hashes.json
grep -Fq '"cursor_sprite":false' scene-tail.json

