#!/usr/bin/env bash

if [[ -n "${GW_VM_CONFIG_LOADED:-}" ]]; then
  return 0
fi
GW_VM_CONFIG_LOADED=1

trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

resolve_config_path() {
  local selected
  if [[ -n "${GW_VM_CONFIG_CLI:-}" ]]; then
    selected="$GW_VM_CONFIG_CLI"
  elif [[ -n "${GLASSWYRM_VM_CONFIG:-}" ]]; then
    selected="$GLASSWYRM_VM_CONFIG"
  elif [[ -f "$GW_VM_DIR/config.toml" ]]; then
    selected="$GW_VM_DIR/config.toml"
  else
    selected="$GW_VM_DIR/config.example.toml"
  fi

  if [[ "$selected" != /* ]]; then
    selected="$(pwd)/$selected"
  fi
  CONFIG_PATH="$(realpath -m "$selected")"
}

set_config_value() {
  local section="$1"
  local key="$2"
  local value="$3"

  case "$section.$key" in
    libvirt.uri) LIBVIRT_URI="$value" ;;
    libvirt.domain) VM_DOMAIN="$value" ;;
    snapshot.name) SNAPSHOT_NAME="$value" ;;
    snapshot.enabled) SNAPSHOT_ENABLED="$value" ;;
    guest.ssh_host) SSH_HOST="$value" ;;
    guest.ssh_user) SSH_USER="$value" ;;
    guest.ssh_port) SSH_PORT="$value" ;;
    guest.shared_overlay_path) GUEST_OVERLAY_PATH="$value" ;;
    guest.shared_artifacts_path) GUEST_ARTIFACTS_PATH="$value" ;;
    paths.overlay) LOCAL_OVERLAY_PATH="$value" ;;
    paths.artifacts) ARTIFACTS_PATH="$value" ;;
    portage.overlay_name) OVERLAY_NAME="$value" ;;
    portage.repos_conf_path) REPOS_CONF_PATH="$value" ;;
    portage.default_package) DEFAULT_PACKAGE="$value" ;;
    safety.allow_destructive) ALLOW_DESTRUCTIVE="$value" ;;
    safety.allow_arbitrary_ssh) ALLOW_ARBITRARY_SSH="$value" ;;
    *) die "Unknown configuration key: $section.$key" ;;
  esac
}

parse_config_file() {
  local section=""
  local raw line key value

  [[ -f "$CONFIG_PATH" ]] || die "Configuration file not found: $CONFIG_PATH"
  while IFS= read -r raw || [[ -n "$raw" ]]; do
    line="$(trim "${raw%%#*}")"
    [[ -z "$line" ]] && continue
    if [[ "$line" =~ ^\[([A-Za-z0-9_-]+)\]$ ]]; then
      section="${BASH_REMATCH[1]}"
      continue
    fi
    [[ -n "$section" && "$line" == *=* ]] ||
      die "Invalid configuration line in $CONFIG_PATH: $raw"
    key="$(trim "${line%%=*}")"
    value="$(trim "${line#*=}")"
    if [[ "$value" == \"*\" ]]; then
      [[ "$value" == *\" ]] || die "Unterminated string for $section.$key"
      value="${value:1:${#value}-2}"
    elif [[ "$value" != "true" && "$value" != "false" && ! "$value" =~ ^[0-9]+$ ]]; then
      die "Unsupported value for $section.$key"
    fi
    [[ "$value" != *$'\n'* && "$value" != *$'\r'* ]] ||
      die "Control characters are not allowed in configuration values."
    set_config_value "$section" "$key" "$value"
  done <"$CONFIG_PATH"
}

apply_environment_overrides() {
  VM_DOMAIN="${GLASSWYRM_VM_NAME:-$VM_DOMAIN}"
  SSH_HOST="${GLASSWYRM_VM_SSH_HOST:-$SSH_HOST}"
  SSH_USER="${GLASSWYRM_VM_SSH_USER:-$SSH_USER}"
  SSH_PORT="${GLASSWYRM_VM_SSH_PORT:-$SSH_PORT}"
  LIBVIRT_URI="${GLASSWYRM_VM_LIBVIRT_URI:-$LIBVIRT_URI}"
  SNAPSHOT_NAME="${GLASSWYRM_VM_SNAPSHOT:-$SNAPSHOT_NAME}"
  LOCAL_OVERLAY_PATH="${GLASSWYRM_VM_OVERLAY_PATH:-$LOCAL_OVERLAY_PATH}"
  ARTIFACTS_PATH="${GLASSWYRM_VM_ARTIFACTS_PATH:-$ARTIFACTS_PATH}"
}

resolve_repo_path() {
  local value="$1"
  if [[ "$value" == /* ]]; then
    realpath -m "$value"
  else
    realpath -m "$REPO_ROOT/$value"
  fi
}

validate_config() {
  [[ "$LIBVIRT_URI" != -* && -n "$LIBVIRT_URI" ]] || die "Invalid libvirt URI."
  [[ -z "$VM_DOMAIN" || "$VM_DOMAIN" =~ ^[A-Za-z0-9_.:+-]+$ ]] || die "Invalid VM domain."
  [[ -z "$VM_DOMAIN" || "$VM_DOMAIN" != -* ]] || die "Invalid VM domain."
  [[ "$SNAPSHOT_NAME" =~ ^[A-Za-z0-9_.:+-]+$ && "$SNAPSHOT_NAME" != -* ]] ||
    die "Invalid snapshot name."
  [[ "$SNAPSHOT_ENABLED" == "true" || "$SNAPSHOT_ENABLED" == "false" ]] ||
    die "snapshot.enabled must be true or false."
  [[ "$SSH_HOST" =~ ^[A-Za-z0-9_.:-]+$ && "$SSH_HOST" != -* ]] || die "Invalid SSH host."
  [[ "$SSH_USER" =~ ^[A-Za-z_][A-Za-z0-9_-]*$ ]] || die "Invalid SSH user."
  [[ "$SSH_PORT" =~ ^[0-9]+$ ]] && ((10#$SSH_PORT >= 1 && 10#$SSH_PORT <= 65535)) ||
    die "Invalid SSH port."
  [[ "$GUEST_OVERLAY_PATH" =~ ^/[A-Za-z0-9._/-]+$ && "$GUEST_OVERLAY_PATH" != "/" && "$GUEST_OVERLAY_PATH" != *".."* ]] ||
    die "Guest overlay path must be a safe absolute path."
  [[ "$GUEST_ARTIFACTS_PATH" =~ ^/[A-Za-z0-9._/-]+$ && "$GUEST_ARTIFACTS_PATH" != "/" && "$GUEST_ARTIFACTS_PATH" != *".."* ]] ||
    die "Guest artifact path must be a safe absolute path."
  [[ "$OVERLAY_NAME" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "Invalid overlay name."
  [[ "$REPOS_CONF_PATH" =~ ^/etc/portage/repos\.conf/[A-Za-z0-9._-]+\.conf$ ]] ||
    die "repos_conf_path must name a .conf file under /etc/portage/repos.conf/."
  [[ "$ALLOW_DESTRUCTIVE" == "true" || "$ALLOW_DESTRUCTIVE" == "false" ]] ||
    die "safety.allow_destructive must be true or false."
  [[ "$ALLOW_ARBITRARY_SSH" == "true" || "$ALLOW_ARBITRARY_SSH" == "false" ]] ||
    die "safety.allow_arbitrary_ssh must be true or false."
  validate_package "$DEFAULT_PACKAGE"

  LOCAL_OVERLAY_PATH_ABS="$(resolve_repo_path "$LOCAL_OVERLAY_PATH")"
  ARTIFACTS_PATH_ABS="$(resolve_repo_path "$ARTIFACTS_PATH")"
  local artifacts_root
  artifacts_root="$(realpath -m "$REPO_ROOT/artifacts/vm")"
  [[ "$ARTIFACTS_PATH_ABS" == "$artifacts_root" || "$ARTIFACTS_PATH_ABS" == "$artifacts_root/"* ]] ||
    die "Artifact output must remain under artifacts/vm/."

  if [[ "$LOCAL_OVERLAY_PATH" == /* ]]; then
    OVERLAY_PATH_DISPLAY="$LOCAL_OVERLAY_PATH"
  else
    OVERLAY_PATH_DISPLAY="$LOCAL_OVERLAY_PATH"
  fi
  if [[ "$ARTIFACTS_PATH" == /* ]]; then
    ARTIFACTS_PATH_DISPLAY="$ARTIFACTS_PATH"
  else
    ARTIFACTS_PATH_DISPLAY="$ARTIFACTS_PATH"
  fi
}

load_config() {
  LIBVIRT_URI="qemu:///system"
  VM_DOMAIN=""
  SNAPSHOT_NAME="clean-base"
  SNAPSHOT_ENABLED=true
  SSH_HOST="glasswyrm-vm"
  SSH_USER="root"
  SSH_PORT=22
  GUEST_OVERLAY_PATH="/mnt/shared/glasswyrm-overlay"
  GUEST_ARTIFACTS_PATH="/mnt/shared/glasswyrm-artifacts"
  LOCAL_OVERLAY_PATH="packaging/gentoo/overlay"
  ARTIFACTS_PATH="artifacts/vm/latest"
  OVERLAY_NAME="glasswyrm-local"
  REPOS_CONF_PATH="/etc/portage/repos.conf/glasswyrm-local.conf"
  DEFAULT_PACKAGE="x11-base/glasswyrm"
  ALLOW_DESTRUCTIVE=false
  ALLOW_ARBITRARY_SSH=false

  resolve_config_path
  parse_config_file
  apply_environment_overrides
  validate_config
}

require_vm_domain() {
  if [[ -z "$VM_DOMAIN" ]]; then
    printf '%s\n' "No VM domain configured." >&2
    printf '%s\n' "Set GLASSWYRM_VM_NAME or copy tools/gw-vm.d/config.example.toml to tools/gw-vm.d/config.toml and set [libvirt].domain." >&2
    exit 1
  fi
}
