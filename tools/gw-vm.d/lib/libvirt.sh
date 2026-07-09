#!/usr/bin/env bash

vm_status() {
  require_vm_domain
  if ! command_exists virsh; then
    warn "Required host command not found: virsh"
    return 1
  fi
  printf 'Configured domain: %s\n' "$VM_DOMAIN"
  printf 'Libvirt URI: %s\n' "$LIBVIRT_URI"
  printf 'Guest SSH target: %s@%s:%s\n' "$SSH_USER" "$SSH_HOST" "$SSH_PORT"
  printf 'Overlay source: %s\n' "$OVERLAY_PATH_DISPLAY"
  printf 'Artifact output: %s\n' "$ARTIFACTS_PATH_DISPLAY"
  virsh --connect "$LIBVIRT_URI" domstate "$VM_DOMAIN"
}

vm_boot() {
  require_vm_domain
  if ! command_exists virsh; then
    warn "Required host command not found: virsh"
    return 1
  fi
  local state=""
  state="$(LC_ALL=C virsh --connect "$LIBVIRT_URI" domstate "$VM_DOMAIN" 2>/dev/null || true)"
  case "$state" in
    running|idle|paused)
      note "VM '$VM_DOMAIN' is already $state."
      ;;
    *)
      virsh --connect "$LIBVIRT_URI" start "$VM_DOMAIN" || return
      ;;
  esac
  record_scenario "boot" "passed" ""
}

vm_shutdown() {
  require_vm_domain
  if ! command_exists virsh; then
    warn "Required host command not found: virsh"
    return 1
  fi
  virsh --connect "$LIBVIRT_URI" shutdown "$VM_DOMAIN" || return
  record_scenario "shutdown" "passed" ""
}

vm_reset() {
  local approved="$1"
  require_approval "reset" "$approved"
  require_vm_domain
  if ! is_true "$SNAPSHOT_ENABLED"; then
    note "Snapshot reset is disabled; skipping."
    record_scenario "reset" "skipped" ""
    return 0
  fi
  if ! command_exists virsh; then
    warn "Required host command not found: virsh"
    return 1
  fi
  virsh --connect "$LIBVIRT_URI" snapshot-revert "$VM_DOMAIN" "$SNAPSHOT_NAME" || return
  record_scenario "reset" "passed" ""
}
