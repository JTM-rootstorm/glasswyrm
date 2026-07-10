#!/usr/bin/env bash
set -euo pipefail
exec "$(cd "$(dirname "$0")/../.." && pwd)/gw-vm" scenario milestone1-runtime-test "$@"
