#!/bin/sh
set -eu

root=$1
fixtures="$root/tests/fixtures/m9"

cd "$fixtures"
sha256sum --check SHA256SUMS

for fixture in *.trace.json; do
  python3 -m json.tool "$fixture" >/dev/null
done
python3 -m json.tool frame-hashes.json >/dev/null
