#!/usr/bin/env bash

ssh_arguments() {
  SSH_ARGS=(
    -p "$SSH_PORT"
    -o BatchMode=yes
    -o ConnectTimeout=10
  )
  SSH_TARGET="$SSH_USER@$SSH_HOST"
}

guest_run_script() {
  local script="$1"
  shift
  if ! command_exists ssh; then
    warn "Required host command not found: ssh"
    return 1
  fi
  ssh_arguments
  ssh "${SSH_ARGS[@]}" "$SSH_TARGET" bash -s -- "$@" <<<"$script"
}

wait_for_guest() {
  if ! command_exists ssh; then
    warn "Required host command not found: ssh"
    return 1
  fi
  local attempt
  SSH_TARGET="$SSH_USER@$SSH_HOST"
  for attempt in {1..15}; do
    if ssh -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=2 \
      "$SSH_TARGET" true </dev/null >/dev/null 2>&1; then
      return 0
    fi
    sleep 2
  done
  warn "Guest SSH did not become ready at $SSH_TARGET within 60 seconds."
  return 1
}

prepare_overlay_destination() {
  local script
  script='set -euo pipefail
destination=$1
marker="$destination/.glasswyrm-vm-overlay"
if [[ -d "$destination" && ! -e "$marker" ]]; then
  if find "$destination" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
    echo "Refusing to replace unowned overlay destination: $destination" >&2
    exit 1
  fi
fi
mkdir -p "$destination"
touch "$marker"'
  guest_run_script "$script" "$GUEST_OVERLAY_PATH" || return
}

push_overlay() {
  if ! command_exists rsync; then
    warn "Required host command not found: rsync"
    return 1
  fi
  if [[ ! -d "$LOCAL_OVERLAY_PATH_ABS" ]]; then
    warn "Overlay source does not exist: $OVERLAY_PATH_DISPLAY"
    return 1
  fi
  wait_for_guest || return
  prepare_overlay_destination || return
  ssh_arguments
  rsync -a --delete --filter='P /.glasswyrm-vm-overlay' \
    -e "ssh -p $SSH_PORT -o BatchMode=yes -o ConnectTimeout=10" \
    "$LOCAL_OVERLAY_PATH_ABS/" "$SSH_TARGET:$GUEST_OVERLAY_PATH/" || return
  record_scenario "push-overlay" "passed" ""
}

prepare_source_destination() {
  local script
  script='set -euo pipefail
destination=$1
marker="$destination/.glasswyrm-vm-source"
if [[ -L "$destination" ]]; then
  echo "Refusing symlink source destination: $destination" >&2
  exit 1
fi
if [[ -e "$destination" && ! -d "$destination" ]]; then
  echo "Refusing to replace non-directory source destination: $destination" >&2
  exit 1
fi
if [[ -e "$marker" && ( ! -f "$marker" || -L "$marker" ) ]]; then
  echo "Refusing invalid source ownership marker: $marker" >&2
  exit 1
fi
if [[ -d "$destination" && ! -e "$marker" ]]; then
  if find "$destination" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
    echo "Refusing to replace unowned source destination: $destination" >&2
    exit 1
  fi
fi
mkdir -p "$destination"
touch "$marker"'
  guest_run_script "$script" "$GUEST_SOURCE_PATH" || return
}

push_source() {
  if ! command_exists rsync; then
    warn "Required host command not found: rsync"
    return 1
  fi
  if [[ ! -d "$LOCAL_SOURCE_PATH_ABS" ]]; then
    warn "Source path does not exist: $LOCAL_SOURCE_PATH"
    return 1
  fi
  wait_for_guest || return
  prepare_source_destination || return
  ssh_arguments
  rsync -a --delete \
    --filter='P /.glasswyrm-vm-source' \
    --filter='- /.git/' \
    --filter='- /Plans/' \
    --filter='- /artifacts/' \
    --filter='- /build/' \
    --filter='- /build-*/' \
    --filter='- /builddir/' \
    --filter='- /_build/' \
    --filter='- /tools/gw-vm.d/config.toml' \
    -e "ssh -p $SSH_PORT -o BatchMode=yes -o ConnectTimeout=10" \
    "$LOCAL_SOURCE_PATH_ABS/" "$SSH_TARGET:$GUEST_SOURCE_PATH/" || return
  record_scenario "push-source" "passed" ""
}

register_overlay() {
  local script
  script='set -euo pipefail
overlay_name=$1
overlay_path=$2
repos_conf=$3
mkdir -p "${repos_conf%/*}"
{
  printf "[%s]\n" "$overlay_name"
  printf "location = %s\n" "$overlay_path"
  printf "masters = gentoo\n"
  printf "auto-sync = no\n"
} >"$repos_conf"'
  guest_run_script "$script" "$OVERLAY_NAME" "$GUEST_OVERLAY_PATH" "$REPOS_CONF_PATH" || return
  record_scenario "register-overlay" "passed" ""
}

capture_guest_action() {
  local scenario="$1"
  local log_path="$2"
  local script="$3"
  shift 3
  local guest_status tee_status status
  local -a pipe_status

  init_artifacts
  set +e
  guest_run_script "$script" "$@" 2>&1 | tee "$log_path"
  pipe_status=("${PIPESTATUS[@]}")
  guest_status=${pipe_status[0]}
  tee_status=${pipe_status[1]}
  set -e
  status=$guest_status
  if ((status == 0 && tee_status != 0)); then
    status=$tee_status
  fi
  if ((status == 0)); then
    record_scenario "$scenario" "passed" "$log_path"
  else
    record_scenario "$scenario" "failed" "$log_path"
    printf 'VM scenario failed: %s\n' "$scenario" >&2
    print_artifacts >&2
  fi
  return "$status"
}

portage_action() {
  local action="$1"
  local package="${2:-}"
  local scenario log_name script
  script='set -euo pipefail
action=$1
package=${2:-}
export NOCOLOR=1
case "$action" in
  metadata) exec emerge --metadata --color=n ;;
  pretend) exec emerge --pretend --verbose --tree --color=n "$package" ;;
  emerge) exec emerge --verbose --color=n "$package" ;;
  unmerge) exec emerge --unmerge --color=n "$package" ;;
  *) echo "Unknown fixed Portage action: $action" >&2; exit 2 ;;
esac'

  case "$action" in
    metadata)
      scenario="emerge-metadata"
      log_name="emerge-metadata.log"
      ;;
    pretend|emerge|unmerge)
      validate_package "$package"
      scenario="$action-$package"
      log_name="$action-$(sanitize_package "$package").log"
      ;;
    *) die "Unknown Portage action: $action" ;;
  esac

  capture_guest_action "$scenario" "$ARTIFACTS_PATH_ABS/$log_name" \
    "$script" "$action" "$package"
}
