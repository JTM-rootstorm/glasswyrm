#!/usr/bin/env bash
set -euo pipefail
exec "$(cd "$(dirname "$0")/../.." && pwd)/gw-vm" scenario narrow-test-gwm "$@"
