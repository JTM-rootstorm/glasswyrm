#!/usr/bin/env bash
exec "$(cd "$(dirname "$0")/../.." && pwd)/gw-vm" scenario milestone4-runtime-test "$@"
