#!/usr/bin/env bash

rust_transition_guest_script() {
  cat <<'EOF'
set -euo pipefail
source_dir=$1

case "$source_dir" in
  /*) ;;
  *) printf 'Rust transition source path must be absolute: %s\n' "$source_dir" >&2; exit 20 ;;
esac
if [[ "$source_dir" == / || -L "$source_dir" || ! -d "$source_dir" ]]; then
  printf 'Refusing unsafe Rust transition source path: %s\n' "$source_dir" >&2
  exit 20
fi
canonical_source=$(readlink -f -- "$source_dir")
if [[ "$canonical_source" != "$source_dir" ]]; then
  printf 'Rust transition source path must be canonical: %s\n' "$source_dir" >&2
  exit 20
fi
marker=$source_dir/.glasswyrm-vm-source
if [[ ! -f "$marker" || -L "$marker" ]]; then
  printf 'Owned source marker is missing or invalid: %s\n' "$marker" >&2
  exit 20
fi
if [[ ! -f "$source_dir/Cargo.toml" || ! -f "$source_dir/Cargo.lock" ]]; then
  printf 'Rust workspace manifests are missing beneath owned source: %s\n' "$source_dir" >&2
  exit 20
fi

unset GW_ALLOW_HARDWARE_TESTS
export CARGO_TERM_COLOR=never
export RUSTUP_AUTO_INSTALL=0
cd "$source_dir"

for tool in cargo rustc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Required guest Rust tool is missing: %s\n' "$tool" >&2
    exit 21
  fi
done
if ! cargo fmt --version >/dev/null 2>&1; then
  printf 'Required guest Rust component is missing: rustfmt\n' >&2
  exit 21
fi
if ! cargo clippy --version >/dev/null 2>&1; then
  printf 'Required guest Rust component is missing: clippy\n' >&2
  exit 21
fi

printf 'rust-transition-stage=toolchain\n'
rustc --version
cargo --version
cargo fmt --version
cargo clippy --version

printf 'rust-transition-stage=fmt\n'
cargo fmt --all -- --check
printf 'rust-transition-stage=check\n'
cargo check --workspace --all-targets --locked
printf 'rust-transition-stage=test\n'
cargo test --workspace --all-targets --locked
printf 'rust-transition-stage=clippy\n'
cargo clippy --workspace --all-targets --locked -- -D warnings
printf 'rust-transition-stage=complete\n'
EOF
}

write_rust_transition_summary() {
  local passed=$1 status=$2 log_path=$3
  local summary_path="$ARTIFACTS_PATH_ABS/rust-transition-software-test.json"
  local temporary_path="$summary_path.tmp"
  python3 - "$temporary_path" "$passed" "$status" "$log_path" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
payload = {
    "schema": 1,
    "scenario": "rust-transition-software-test",
    "passed": sys.argv[2] == "true",
    "exit_status": int(sys.argv[3]),
    "log": sys.argv[4],
    "hardware_authorized": False,
    "stages": ["fmt", "check", "test", "clippy"],
}
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
  mv "$temporary_path" "$summary_path"
}

rust_transition_software_test() {
  init_artifacts
  local log_path="$ARTIFACTS_PATH_ABS/rust-transition-software-test.log"
  local script status=0 passed=true
  script=$(rust_transition_guest_script)

  if capture_guest_action rust-transition-software-test "$log_path" \
    "$script" "$GUEST_SOURCE_PATH"; then
    :
  else
    status=$?
    passed=false
  fi

  write_rust_transition_summary "$passed" "$status" "$log_path" || return
  if ((status != 0)); then
    return "$status"
  fi
  printf 'Rust transition VM software test passed.\n'
  print_artifacts
}
