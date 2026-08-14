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
printf 'rust-transition-tool-rustc=%s\n' "$(rustc --version)"
printf 'rust-transition-tool-cargo=%s\n' "$(cargo --version)"
printf 'rust-transition-tool-rustfmt=%s\n' "$(cargo fmt --version)"
printf 'rust-transition-tool-clippy=%s\n' "$(cargo clippy --version)"

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

rust_transition_source_status_ignored() {
  local line=$1
  [[ $line == '?? Plans/'* || $line == '?? .codex/'* ||
     $line == '?? m14' || $line == '?? m14-export' ||
     $line == '?? '*'/__pycache__/'*.pyc ]]
}

verify_rust_transition_source_identity() {
  local status unexpected='' line current
  status=$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all) || return
  while IFS= read -r line; do
    [[ -z $line ]] || rust_transition_source_status_ignored "$line" ||
      unexpected+="${unexpected:+$'\n'}$line"
  done <<<"$status"
  [[ -z $unexpected ]] || {
    printf 'Rust transition VM acceptance requires committed source outside local operator material.\n%s\n' \
      "$unexpected" >&2
    return 1
  }
  current=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  if [[ -n ${RUST_TRANSITION_TESTED_COMMIT:-} &&
        $current != "$RUST_TRANSITION_TESTED_COMMIT" ]]; then
    printf 'Rust transition source commit changed during acceptance: expected %s, found %s.\n' \
      "$RUST_TRANSITION_TESTED_COMMIT" "$current" >&2
    return 1
  fi
}

prepare_rust_transition_evidence() {
  RUST_TRANSITION_TESTED_COMMIT=$(git -C "$REPO_ROOT" rev-parse HEAD) || return
  verify_rust_transition_source_identity
}

write_rust_transition_summary() {
  local passed=$1 status=$2 log_path=$3 tested_commit=$4
  local summary_path="$ARTIFACTS_PATH_ABS/rust-transition-software-test.json"
  local temporary_path="$summary_path.tmp"
  python3 - "$temporary_path" "$passed" "$status" "$log_path" \
    "$tested_commit" <<'PY'
import datetime
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
log_path = pathlib.Path(sys.argv[4])
tool_versions = {}
if log_path.is_file():
    prefix = "rust-transition-tool-"
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            name, value = line[len(prefix):].split("=", 1)
            if name in {"rustc", "cargo", "rustfmt", "clippy"}:
                tool_versions[name] = value
payload = {
    "schema": 1,
    "scenario": "rust-transition-software-test",
    "passed": sys.argv[2] == "true",
    "exit_status": int(sys.argv[3]),
    "log": sys.argv[4],
    "tested_commit": sys.argv[5],
    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z"),
    "tool_versions": tool_versions,
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
  prepare_rust_transition_evidence || return
  : >"$log_path"

  if push_source; then
    :
  else
    status=$?
    passed=false
  fi
  if ((status == 0)) && ! verify_rust_transition_source_identity; then
    status=22
    passed=false
  fi

  script=$(rust_transition_guest_script)

  if ((status == 0)); then
    if capture_guest_action rust-transition-software-test "$log_path" \
      "$script" "$GUEST_SOURCE_PATH"; then
      :
    else
      status=$?
      passed=false
    fi
  fi
  if ! verify_rust_transition_source_identity; then
    ((status != 0)) || status=22
    passed=false
  fi

  write_rust_transition_summary "$passed" "$status" "$log_path" \
    "$RUST_TRANSITION_TESTED_COMMIT" || return
  if ((status != 0)); then
    return "$status"
  fi
  printf 'Rust transition VM software test passed.\n'
  print_artifacts
}
