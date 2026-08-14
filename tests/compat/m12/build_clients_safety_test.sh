#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  printf 'Usage: %s REPOSITORY_ROOT\n' "$0" >&2
  exit 2
fi

helper=$1/tests/compat/m12/build_clients.sh
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
root=$temporary/clients
outside=$temporary/outside
mkdir -p "$root/source" "$root/build" "$root/install" "$outside"
: >"$root/source/preserve"
: >"$outside/preserve"

expect_rejected() {
  local label=$1
  shift
  if "$helper" "$temporary/archive.tar.gz" "$@" \
      >"$temporary/$label.out" 2>&1; then
    printf 'Unsafe client cleanup was accepted: %s\n' "$label" >&2
    exit 1
  fi
  [[ -f $root/source/preserve && -f $outside/preserve ]] || {
    printf 'Rejected client cleanup modified files: %s\n' "$label" >&2
    exit 1
  }
}

expect_rejected missing-marker "$root/source" "$root/build" "$root/install"
printf '%s\n' glasswyrm-m12-client-build-root-v1 \
  >"$root/.glasswyrm-m12-client-build-root"
expect_rejected broad-target "$root/source" "$root/build" "$root"
expect_rejected noncanonical "$root/./source" "$root/build" "$root/install"
expect_rejected filesystem-root /source /build /install

mv "$root/source" "$root/source-real"
ln -s "$outside" "$root/source"
expect_rejected symlink-target "$root/source" "$root/build" "$root/install"

printf '%s\n' 'M12 client build cleanup safety tests passed'
