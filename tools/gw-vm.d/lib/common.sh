#!/usr/bin/env bash

if [[ -n "${GW_VM_COMMON_LOADED:-}" ]]; then
  return 0
fi
GW_VM_COMMON_LOADED=1

GW_VM_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GW_VM_DIR="$(cd "$GW_VM_LIB_DIR/.." && pwd)"
REPO_ROOT="$(cd "$GW_VM_DIR/../.." && pwd)"

SCENARIO_RECORDS=()

die() {
  printf 'gw-vm: %s\n' "$*" >&2
  exit 1
}

warn() {
  printf 'warning: %s\n' "$*" >&2
}

note() {
  printf '%s\n' "$*"
}

command_exists() {
  command -v "$1" >/dev/null 2>&1
}

require_command() {
  command_exists "$1" || die "Required host command not found: $1"
}

is_true() {
  [[ "$1" == "true" ]]
}

semantic_version_at_least() {
  local actual=$1 minimum=$2
  local actual_major actual_minor actual_patch
  local minimum_major minimum_minor minimum_patch
  [[ $actual =~ ^([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})$ ]] || return 1
  actual_major=$((10#${BASH_REMATCH[1]}))
  actual_minor=$((10#${BASH_REMATCH[2]}))
  actual_patch=$((10#${BASH_REMATCH[3]}))
  ((actual_major <= 65535 && actual_minor <= 65535 && actual_patch <= 65535)) || return 1
  [[ $minimum =~ ^([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})$ ]] || return 1
  minimum_major=$((10#${BASH_REMATCH[1]}))
  minimum_minor=$((10#${BASH_REMATCH[2]}))
  minimum_patch=$((10#${BASH_REMATCH[3]}))
  ((minimum_major <= 65535 && minimum_minor <= 65535 && minimum_patch <= 65535)) || return 1

  ((actual_major > minimum_major)) ||
    ((actual_major == minimum_major && actual_minor > minimum_minor)) ||
    ((actual_major == minimum_major && actual_minor == minimum_minor &&
      actual_patch >= minimum_patch))
}

require_no_extra_args() {
  local command_name="$1"
  shift
  (($# == 0)) || die "Command '$command_name' does not accept: $*"
}

require_approval() {
  local action="$1"
  local approved="$2"
  if [[ "$approved" != "true" ]] && ! is_true "$ALLOW_DESTRUCTIVE"; then
    die "Action '$action' requires --yes or safety.allow_destructive = true."
  fi
}

parse_yes_only() {
  local approved=false
  while (($# > 0)); do
    case "$1" in
      --yes) approved=true ;;
      *) die "Unknown option for destructive action: $1" ;;
    esac
    shift
  done
  printf '%s\n' "$approved"
}

validate_package() {
  local package="$1"
  [[ "$package" =~ ^[A-Za-z0-9+_.-]+/[A-Za-z0-9+_@.-]+$ ]] ||
    die "Invalid package atom '$package'; expected category/package without operators or flags."
}

sanitize_package() {
  local package="$1"
  validate_package "$package"
  printf '%s\n' "${package//\//-}"
}

init_artifacts() {
  mkdir -p "$ARTIFACTS_PATH_ABS" ||
    die "Cannot create artifact directory: $ARTIFACTS_PATH_DISPLAY"
}

record_scenario() {
  local name="$1"
  local status="$2"
  local log_path="${3:-}"
  SCENARIO_RECORDS+=("$name"$'\t'"$status"$'\t'"$log_path")
}

print_artifacts() {
  printf 'Artifacts: %s\n' "$ARTIFACTS_PATH_DISPLAY"
}
