#!/usr/bin/env bash
# shellcheck disable=SC2016 # Assertions intentionally match literal guest scripts.

set -euo pipefail

repo_root=${1:-}
if [[ -z ${repo_root} ]]; then
  printf 'usage: %s REPOSITORY_ROOT\n' "$0" >&2
  exit 2
fi

gw_vm=${repo_root}/tools/gw-vm
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/glasswyrm-gw-vm-test.XXXXXX")

fake_bin=${work_dir}/bin
mkdir -p "$repo_root/artifacts/vm"
artifact_dir=$(mktemp -d "$repo_root/artifacts/vm/cli-test.XXXXXX")
trap 'rm -rf "$work_dir" "$artifact_dir"' EXIT
overlay_dir=${work_dir}/overlay
source_dir=${artifact_dir}/source
guest_source_dir=${work_dir}/guest-source
command_log=${work_dir}/commands.log
config_file=${work_dir}/config.toml
no_domain_config=${work_dir}/no-domain.toml
mkdir -p "$fake_bin" "$artifact_dir" "$overlay_dir" "$source_dir"
touch "$source_dir/meson.build"

fail() {
  printf 'gw-vm CLI test failed: %s\n' "$*" >&2
  exit 1
}

run_success() {
  local output_file=$1
  shift
  if ! "$@" >"$output_file" 2>&1; then
    sed 's/^/  /' "$output_file" >&2
    fail "command unexpectedly failed: $*"
  fi
}

run_failure() {
  local output_file=$1
  shift
  if "$@" >"$output_file" 2>&1; then
    sed 's/^/  /' "$output_file" >&2
    fail "command unexpectedly succeeded: $*"
  fi
}

run_allow_failure() {
  local output_file=$1
  shift
  "$@" >"$output_file" 2>&1 || true
}

assert_contains() {
  local file=$1
  local expected=$2
  grep -F -- "$expected" "$file" >/dev/null || {
    sed 's/^/  /' "$file" >&2
    fail "$file does not contain: $expected"
  }
}

assert_not_contains() {
  local file=$1
  local unexpected=$2
  if grep -F -- "$unexpected" "$file" >/dev/null; then
    sed 's/^/  /' "$file" >&2
    fail "$file unexpectedly contains: $unexpected"
  fi
}

assert_file_glob() {
  local pattern=$1
  compgen -G "$pattern" >/dev/null || fail "no file matched: $pattern"
}

semver_helper=${repo_root}/tools/gw-vm.d/lib/common.sh
for compatible in '0.1.0 0.1.0' '0.2.0 0.1.0' '0.10.0 0.3.0' \
  '1.0.0 0.3.0' '0.3.1 0.3.0'; do
  read -r actual minimum <<<"$compatible"
  bash -c 'source "$1"; semantic_version_at_least "$2" "$3"' _ \
    "$semver_helper" "$actual" "$minimum" ||
    fail "semantic version helper rejected $actual >= $minimum"
done
for incompatible in '0.0.9 0.1.0' '0.2.9 0.3.0' '0.3 0.3.0' \
  'unknown 0.1.0' '0.3.0 0.3'; do
  read -r actual minimum <<<"$incompatible"
  if bash -c 'source "$1"; semantic_version_at_least "$2" "$3"' _ \
    "$semver_helper" "$actual" "$minimum"; then
    fail "semantic version helper accepted $actual >= $minimum"
  fi
done

cat >"$config_file" <<EOF
[libvirt]
uri = "test:///glasswyrm"
domain = "glasswyrm-test"

[snapshot]
name = "pristine"
enabled = true

[guest]
ssh_host = "guest.test"
ssh_user = "root"
ssh_port = 2222
shared_overlay_path = "/mnt/shared/glasswyrm-overlay"
shared_artifacts_path = "/mnt/shared/glasswyrm-artifacts"
shared_source_path = "$guest_source_dir"

[paths]
overlay = "$overlay_dir"
artifacts = "$artifact_dir"
source = "$source_dir"

[portage]
overlay_name = "glasswyrm-test"
repos_conf_path = "/etc/portage/repos.conf/glasswyrm-test.conf"
default_package = "x11-base/glasswyrm"

[safety]
allow_destructive = false
allow_arbitrary_ssh = false
EOF

sed 's/domain = "glasswyrm-test"/domain = ""/' "$config_file" >"$no_domain_config"

cat >"$fake_bin/virsh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'virsh' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ -n ${GW_VM_TEST_FAIL_VIRSH:-} && " $* " == *" $GW_VM_TEST_FAIL_VIRSH "* ]]; then
  printf 'injected virsh failure for %s\n' "$GW_VM_TEST_FAIL_VIRSH" >&2
  exit 41
fi
case " $* " in
  *' domstate '*) printf 'running\n' ;;
  *' start '*) printf 'Domain started\n' ;;
  *' shutdown '*) printf 'Domain is being shutdown\n' ;;
  *' snapshot-revert '*) printf 'Snapshot reverted\n' ;;
esac
EOF

cat >"$fake_bin/ssh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'ssh' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"

script=$(cat)
guest_args=()
after_separator=false
for argument in "$@"; do
  if [[ $after_separator == true ]]; then
    guest_args+=("$argument")
  elif [[ $argument == -- ]]; then
    after_separator=true
  fi
done

if [[ $script == *'action=$1'* ]]; then
  action=${guest_args[0]:-}
  package=${guest_args[1]:-}
  printf 'guest-portage <%s> <%s>\n' "$action" "$package" >>"$GW_VM_TEST_COMMAND_LOG"
  if [[ ${GW_VM_TEST_FAIL_MATCH:-} == "$action" ]]; then
    printf 'injected guest failure for %s\n' "$action" >&2
    exit 42
  fi

  case "$action:$package" in
    pretend:x11-wm/gwm)
      printf '[ebuild  N     ] x11-wm/gwm-0.1.0\n'
      if [[ ${GW_VM_TEST_ABI_REBUILDS:-0} == 1 ]]; then
        printf '[ebuild  rR    ] x11-base/glasswyrmd-0.1.0\n'
        printf '[ebuild  rR    ] x11-base/gwcomp-0.1.0\n'
      fi
      ;;
    pretend:*) printf '[ebuild  N     ] x11-base/glasswyrm-0.1.0\n' ;;
    metadata:*) printf 'Regenerating cache entries...\n' ;;
    unmerge:*) printf 'Unmerging package...\n' ;;
    emerge:*) printf 'Emerging package...\n' ;;
  esac
  exit 0
fi

if [[ $script == *'marker="$destination/.glasswyrm-vm-source"'* ]]; then
  printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
  bash -s -- "${guest_args[@]}" <<<"$script"
  exit $?
fi

if [[ $script == *'cat "$1"'* ]]; then
  artifact=${guest_args[0]:-}
  case ${artifact##*/} in
    milestone1-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
x_servers_absent=true
meson_tests=passed
sanitizer=passed
raw_clients=passed
xcb_probe=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_FACTS:-0} == 1 ]]; then
        printf 'x_servers_absent=false\nscenario_exit=1\n'
      fi
      ;;
    milestone1-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_JOURNAL:-0} != 1 ]]; then
        printf 'collected current invocation journal\n'
      fi
      ;;
    milestone1-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone2-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
xcb_proto=x11-base/xcb-proto-1.17.0
x_servers_absent=true
m1_regression_tests=passed
m2_tests=passed
sanitizer=passed
raw_little=passed
raw_big=passed
error_continuation=passed
resource_cleanup=passed
cross_endian_property=passed
xcb_setup=passed
xcb_m2=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M2_FACTS:-0} == 1 ]]; then
        printf 'resource_cleanup=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone2-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M2_JOURNAL:-0} != 1 ]]; then
        printf 'collected current M2 invocation journal\n'
      fi
      ;;
    milestone2-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone3-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
FACTS
      printf 'api_version=%s\n' "${GW_VM_TEST_M3_API_VERSION:-0.1.0}"
      cat <<'FACTS'
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
ipc_only=passed
install_layout=passed
c_consumer=passed
cpp_consumer=passed
handshake=passed
ping_pong=passed
contract_roundtrip=passed
fd_transfer=passed
snapshot=passed
version_rejection=passed
role_rejection=passed
capability_rejection=passed
malformed_isolation=passed
sequence_isolation=passed
limit_isolation=passed
systemd_runtime=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M3_FACTS:-0} == 1 ]]; then
        printf 'sequence_isolation=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone3-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M3_JOURNAL:-0} != 1 ]]; then
        printf 'collected current M3 invocation journal\n'
      fi
      ;;
    milestone3-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone5-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.11.1
ninja_version=1.13.2
systemd_version=systemd 257
api_version=0.3.0
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
gwm_only=passed
ipc_only=passed
legacy_consumers=passed
policy_consumers=passed
basic=passed
snapshot_order=passed
transient=passed
override_redirect=passed
focus=passed
stacking=passed
fullscreen=passed
maximize_minimize=passed
incremental_update=passed
invalid_context_isolation=passed
invalid_window_isolation=passed
unknown_reference_recovery=passed
cycle_rejection=passed
snapshot_abort=passed
malformed_peer_isolation=passed
reconnect_equality=passed
golden_hash_count=10
policy_archive=passed
systemd_runtime=passed
socket_cleanup=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M5_FACTS:-0} == 1 ]]; then printf 'snapshot_abort=failed\nscenario_exit=1\n'; fi
      if [[ ${GW_VM_TEST_BAD_M5_API:-0} == 1 ]]; then printf 'api_version=0.2.0\n'; fi
      ;;
    milestone5-journal.log)
      if [[ ${GW_VM_TEST_EMPTY_M5_JOURNAL:-0} != 1 ]]; then printf 'collected current M5 invocation journal\n'; fi
      ;;
    milestone5-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone6-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.4.0
soversion=0
wire_version=1.0
x_servers_absent=true
full_tests=passed
sanitizer=passed
runtime_build=passed
server_standalone=passed
server_ipc=passed
gwm_only=passed
gwcomp_only=passed
ipc_only=passed
api04_consumers=passed
integrated_little_big=passed
no_ppm=passed
scene_archive=passed
restart_probe=passed
xcb_m6_probe=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M6_FACTS:-0} == 1 ]]; then
        printf 'restart_probe=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone6-restart.json)
      printf '{"completed":true,"connection_preserved":true}\n'
      ;;
    milestone6-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    milestone7-facts.env)
      cat <<'FACTS'
failure_stage=
scenario_exit=0
api_version=0.4.0
soversion=0
wire_version=1.0
compiler_c=gcc test
compiler_cxx=g++ test
meson_version=1.7.0
ninja_version=1.12.0
systemd_version=systemd 257
xcb_proto=x11-base/xcb-proto-1.17.0
scene_manifest=absent
x_servers_absent=true
full_tests=passed
sanitizer=passed
runtime_build=passed
server_standalone=passed
server_ipc=passed
gwm_only=passed
gwcomp_only=passed
ipc_only=passed
api04_consumers=passed
m4_pixel_regression=passed
m5_policy_regression=passed
m6_metadata_regression=passed
raw_little=passed
raw_big=passed
image_byte_order=passed
exposure_events=passed
malformed_x11=passed
malformed_gwipc=passed
x11_resources=passed
raster_requests=passed
plane_mask=passed
damage_coalescing=passed
xcb_drawing=passed
final_frame_golden=passed
buffer_release=passed
compositor_restart=passed
gwm_restart=passed
connection_survival=passed
replay_hash=passed
post_restart_hash=passed
post_restart_drawing=passed
rendering_archive=passed
FACTS
      if [[ ${GW_VM_TEST_BAD_M7_FACTS:-0} == 1 ]]; then
        printf 'final_frame_golden=failed\nscenario_exit=1\n'
      fi
      if [[ ${GW_VM_TEST_BAD_M7_REPLAY:-0} == 1 ]]; then
        printf 'replay_hash=failed\nscenario_exit=1\n'
      fi
      ;;
    milestone7-*.log) printf 'collected %s\n' "${artifact##*/}" ;;
    *) printf 'unexpected artifact request: %s\n' "$artifact" >&2; exit 44 ;;
  esac
  exit 0
fi

printf 'guest-script <%s>\n' "${script//$'\n'/ }" >>"$GW_VM_TEST_COMMAND_LOG"
if [[ -n ${GW_VM_TEST_FAIL_MATCH:-} && $script == *"$GW_VM_TEST_FAIL_MATCH"* ]]; then
  printf 'injected guest failure for %s\n' "$GW_VM_TEST_FAIL_MATCH" >&2
  exit 42
fi

case "$script" in
  *'emerge --info'*) printf 'Portage 3.0 test guest\n' ;;
  *'eselect profile list'*) printf '  [1] default/linux/amd64/23.0 *\n' ;;
  *'qlist -Iv'*) printf 'sys-apps/portage-3.0\nx11-base/glasswyrm-0.1.0\n' ;;
  *'/var/log/emerge.log'*) printf 'test emerge log\n' ;;
  *'/var/log/portage/elog/summary.log'*) printf 'test portage elog\n' ;;
esac
EOF

cat >"$fake_bin/rsync" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'rsync' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ ${GW_VM_TEST_FAIL_RSYNC:-0} == 1 ]]; then
  printf 'injected rsync failure\n' >&2
  exit 43
fi
EOF

cat >"$fake_bin/scp" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'scp' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
if [[ "$*" == *milestone5-policies.tar* ]]; then
  destination=${!#}
  scratch=$(mktemp -d)
  printf '{}\n' >"$scratch/basic.json"
  (cd "$scratch" && sha256sum basic.json >SHA256SUMS && tar -cf "$destination" basic.json SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone6-lifecycle.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  printf '{}\n' >"$scratch/scene.jsonl"
  (cd "$scratch" && sha256sum scene.jsonl >SHA256SUMS && tar -cf "$destination" scene.jsonl SHA256SUMS)
  rm -rf "$scratch"
fi
if [[ "$*" == *milestone7-rendering.tar* ]]; then
  destination=${!#}; scratch=$(mktemp -d)
  printf 'P6\n1 1\n255\n000' >"$scratch/final.ppm"
  printf '{}\n' >"$scratch/frames.jsonl"
  printf '{}\n' >"$scratch/xcb-result.json"
  printf '{}\n' >"$scratch/result.json"
  (cd "$scratch" && sha256sum final.ppm frames.jsonl xcb-result.json result.json >SHA256SUMS && tar -cf "$destination" final.ppm frames.jsonl xcb-result.json result.json SHA256SUMS)
  rm -rf "$scratch"
fi
EOF

cat >"$fake_bin/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'git' >>"$GW_VM_TEST_COMMAND_LOG"
printf ' <%s>' "$@" >>"$GW_VM_TEST_COMMAND_LOG"
printf '\n' >>"$GW_VM_TEST_COMMAND_LOG"
case " $* " in
  *' status --porcelain --untracked-files=all '*)
    if [[ ${GW_VM_TEST_GIT_DIRTY:-0} == 1 ]]; then
      printf ' M tracked-source.cpp\n'
    fi
    ;;
  *' rev-parse HEAD '*) printf '86dab3c000000000000000000000000000000000\n' ;;
  *' merge-base --is-ancestor '*)
    [[ ${GW_VM_TEST_BAD_BASE:-0} != 1 ]]
    ;;
  *) printf 'unexpected fake git command: %s\n' "$*" >&2; exit 45 ;;
esac
EOF

chmod +x "$fake_bin/virsh" "$fake_bin/ssh" "$fake_bin/rsync" "$fake_bin/scp" "$fake_bin/git"

export PATH="$fake_bin:$PATH"
export GW_VM_TEST_COMMAND_LOG=$command_log
export GLASSWYRM_VM_CONFIG=$config_file
unset GLASSWYRM_VM_NAME GLASSWYRM_VM_SSH_HOST GLASSWYRM_VM_SSH_USER
unset GLASSWYRM_VM_SSH_PORT GLASSWYRM_VM_LIBVIRT_URI GLASSWYRM_VM_SNAPSHOT
unset GLASSWYRM_VM_OVERLAY_PATH GLASSWYRM_VM_ARTIFACTS_PATH

[[ -x $gw_vm ]] || fail "$gw_vm is missing or not executable"

run_success "$work_dir/help.out" "$gw_vm" help
for command in doctor status reset pretend emerge unmerge narrow-test collect full-packaging-test push-source milestone1-runtime-test milestone2-runtime-test milestone3-runtime-test milestone4-runtime-test milestone5-runtime-test milestone6-runtime-test milestone7-runtime-test; do
  assert_contains "$work_dir/help.out" "$command"
done

run_allow_failure "$work_dir/doctor.out" env GLASSWYRM_VM_CONFIG="$no_domain_config" "$gw_vm" doctor
assert_contains "$work_dir/doctor.out" 'GLASSWYRM_VM_NAME'

run_failure "$work_dir/status-no-domain.out" env GLASSWYRM_VM_CONFIG="$no_domain_config" "$gw_vm" status
assert_contains "$work_dir/status-no-domain.out" 'No VM domain configured.'
assert_contains "$work_dir/status-no-domain.out" 'GLASSWYRM_VM_NAME'

: >"$command_log"
run_success "$work_dir/status.out" "$gw_vm" status
assert_contains "$work_dir/status.out" 'glasswyrm-test'
assert_contains "$work_dir/status.out" 'test:///glasswyrm'
assert_contains "$work_dir/status.out" 'root@guest.test'
assert_contains "$command_log" 'virsh <--connect> <test:///glasswyrm> <domstate> <glasswyrm-test>'

: >"$command_log"
run_success "$work_dir/status-override.out" env GLASSWYRM_VM_NAME=override-domain "$gw_vm" status
assert_contains "$command_log" '<domstate> <override-domain>'
assert_not_contains "$command_log" '<domstate> <glasswyrm-test>'

: >"$command_log"
run_failure "$work_dir/package-injection.out" "$gw_vm" pretend --help
assert_contains "$work_dir/package-injection.out" 'Invalid package atom'
[[ ! -s $command_log ]] || fail 'invalid package reached a host transport command'

run_failure "$work_dir/scenario-path.out" "$gw_vm" scenario ../pretend-glasswyrm
assert_contains "$work_dir/scenario-path.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-injection.out" "$gw_vm" scenario 'milestone1-runtime-test;touch'
assert_contains "$work_dir/scenario-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m2-injection.out" "$gw_vm" scenario 'milestone2-runtime-test;touch'
assert_contains "$work_dir/scenario-m2-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m3-injection.out" "$gw_vm" scenario 'milestone3-runtime-test;touch'
assert_contains "$work_dir/scenario-m3-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m4-injection.out" "$gw_vm" scenario 'milestone4-runtime-test;touch'
assert_contains "$work_dir/scenario-m4-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m5-injection.out" "$gw_vm" scenario 'milestone5-runtime-test;touch'
assert_contains "$work_dir/scenario-m5-injection.out" 'Scenario names are fixed'

run_failure "$work_dir/scenario-m6-injection.out" "$gw_vm" scenario 'milestone6-runtime-test;touch'
run_failure "$work_dir/scenario-m7-injection.out" "$gw_vm" scenario 'milestone7-runtime-test;touch'
assert_contains "$work_dir/scenario-m6-injection.out" 'Scenario names are fixed'

: >"$command_log"
run_failure "$work_dir/reset-gate.out" "$gw_vm" reset
assert_contains "$work_dir/reset-gate.out" '--yes'
assert_not_contains "$command_log" 'snapshot-revert'
run_success "$work_dir/reset.out" "$gw_vm" reset --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'

: >"$command_log"
run_success "$work_dir/pretend.out" "$gw_vm" pretend x11-base/glasswyrm
assert_contains "$command_log" 'guest-portage <pretend> <x11-base/glasswyrm>'
assert_file_glob "$artifact_dir/pretend-*.log"

rm -f "$artifact_dir/pretend-x11-base-glasswyrm.log"
mkdir "$artifact_dir/pretend-x11-base-glasswyrm.log"
run_failure "$work_dir/pretend-log-failure.out" "$gw_vm" pretend x11-base/glasswyrm
rmdir "$artifact_dir/pretend-x11-base-glasswyrm.log"
assert_contains "$work_dir/pretend-log-failure.out" 'VM scenario failed'

: >"$command_log"
run_failure "$work_dir/emerge-gate.out" "$gw_vm" emerge x11-base/glasswyrm
assert_contains "$work_dir/emerge-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
run_success "$work_dir/emerge.out" "$gw_vm" emerge x11-base/glasswyrm --yes
assert_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_file_glob "$artifact_dir/emerge-*.log"

: >"$command_log"
run_failure "$work_dir/unmerge-gate.out" "$gw_vm" unmerge x11-wm/gwm
assert_contains "$work_dir/unmerge-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
run_success "$work_dir/unmerge.out" "$gw_vm" unmerge x11-wm/gwm --yes
assert_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
assert_file_glob "$artifact_dir/unmerge-*.log"

run_success "$work_dir/narrow-pass.out" "$gw_vm" narrow-test gwm
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": true'
assert_contains "$artifact_dir/narrow-test-gwm.json" '"unexpected_rebuilds": []'

run_failure "$work_dir/narrow-fail.out" env GW_VM_TEST_ABI_REBUILDS=1 "$gw_vm" narrow-test gwm
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": false'
assert_contains "$artifact_dir/narrow-test-gwm.json" 'x11-base/glasswyrmd'
assert_contains "$artifact_dir/narrow-test-gwm.json" 'x11-base/gwcomp'

run_success "$work_dir/narrow-override.out" env GW_VM_TEST_ABI_REBUILDS=1 "$gw_vm" narrow-test gwm --allow-abi-rebuild
assert_contains "$artifact_dir/narrow-test-gwm.json" '"passed": true'

rm -f "$artifact_dir/narrow-test-gwm.log"
mkdir "$artifact_dir/narrow-test-gwm.log"
run_failure "$work_dir/narrow-log-failure.out" "$gw_vm" narrow-test gwm
rmdir "$artifact_dir/narrow-test-gwm.log"

run_success "$work_dir/collect.out" "$gw_vm" collect
for artifact in emerge-info.txt profiles.txt installed-packages.txt emerge-log-tail.txt portage-elog-tail.txt summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "collect did not create $artifact"
done

: >"$command_log"
run_allow_failure "$work_dir/collect-partial.out" env GW_VM_TEST_FAIL_MATCH='emerge --info' "$gw_vm" collect
assert_contains "$command_log" 'emerge --info'
assert_contains "$command_log" 'eselect profile list'
assert_contains "$command_log" 'qlist -Iv'
assert_contains "$artifact_dir/summary.json" '"passed": false'

: >"$command_log"
run_failure "$work_dir/full-gate.out" "$gw_vm" full-packaging-test
assert_contains "$work_dir/full-gate.out" '--yes'
assert_not_contains "$command_log" 'guest-portage <emerge>'
assert_not_contains "$command_log" 'guest-portage <unmerge>'

: >"$command_log"
run_success "$work_dir/full.out" "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'
assert_contains "$command_log" 'rsync'
assert_contains "$command_log" 'guest-portage <metadata> <>'
assert_contains "$command_log" 'guest-portage <pretend> <x11-base/glasswyrm>'
assert_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_contains "$command_log" 'guest-portage <pretend> <x11-wm/gwm>'
assert_contains "$command_log" 'guest-portage <emerge> <x11-wm/gwm>'
assert_contains "$command_log" 'guest-portage <unmerge> <x11-wm/gwm>'
assert_contains "$command_log" 'emerge --info'
assert_contains "$artifact_dir/summary.json" '"passed": true'

run_success "$work_dir/scenario-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/pretend-glasswyrm.sh"

: >"$command_log"
run_failure "$work_dir/full-failure.out" env GW_VM_TEST_FAIL_MATCH=metadata "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" 'guest-portage <metadata> <>'
assert_contains "$command_log" 'emerge --info'
assert_not_contains "$command_log" 'guest-portage <emerge> <x11-base/glasswyrm>'
assert_contains "$work_dir/full-failure.out" 'Artifacts:'

: >"$command_log"
run_failure "$work_dir/full-reset-failure.out" \
  env GW_VM_TEST_FAIL_VIRSH=snapshot-revert "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '<snapshot-revert> <glasswyrm-test> <pristine>'
assert_not_contains "$command_log" 'rsync'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_failure "$work_dir/full-marker-failure.out" \
  env GW_VM_TEST_FAIL_MATCH=.glasswyrm-vm-overlay "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" '.glasswyrm-vm-overlay'
assert_not_contains "$command_log" 'rsync'
assert_not_contains "$command_log" 'guest-portage <metadata>'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_failure "$work_dir/full-rsync-failure.out" \
  env GW_VM_TEST_FAIL_RSYNC=1 "$gw_vm" full-packaging-test --yes
assert_contains "$command_log" 'rsync'
assert_not_contains "$command_log" 'guest-portage <metadata>'
assert_contains "$command_log" 'emerge --info'

: >"$command_log"
run_success "$work_dir/push-source.out" "$gw_vm" push-source
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" "<$source_dir/>"
assert_contains "$command_log" '<--filter=- /.git/>'
assert_contains "$command_log" '<--filter=- /Plans/>'
assert_contains "$command_log" '<--filter=- /artifacts/>'
assert_contains "$command_log" '<--filter=- /build-*/>'
assert_contains "$command_log" '<--filter=- /tools/gw-vm.d/config.toml>'

: >"$command_log"
rm -f "$guest_source_dir/.glasswyrm-vm-source"
mkdir -p "$guest_source_dir"
touch "$guest_source_dir/not-owned"
run_failure "$work_dir/push-source-marker-failure.out" "$gw_vm" push-source
assert_contains "$work_dir/push-source-marker-failure.out" 'Refusing to replace unowned source destination'
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_not_contains "$command_log" 'rsync'
rm -rf "$guest_source_dir"

: >"$command_log"
symlink_target=${work_dir}/symlink-target
mkdir -p "$symlink_target"
touch "$symlink_target/must-survive"
ln -s "$symlink_target" "$guest_source_dir"
run_failure "$work_dir/push-source-symlink-failure.out" "$gw_vm" push-source
assert_contains "$work_dir/push-source-symlink-failure.out" 'Refusing symlink source destination'
assert_not_contains "$command_log" 'rsync'
[[ -f $symlink_target/must-survive ]] || fail 'symlink destination target was modified'
rm -f "$guest_source_dir"

: >"$command_log"
run_failure "$work_dir/milestone1-gate.out" "$gw_vm" milestone1-runtime-test
assert_contains "$work_dir/milestone1-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone1-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$work_dir/milestone1-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone1.out" "$gw_vm" milestone1-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'systemd-run --unit="$unit"'
assert_contains "$command_log" 'portageq match / "$1"'
assert_contains "$command_log" '_SYSTEMD_INVOCATION_ID=$unit_invocation_id'
assert_contains "$command_log" 'x11_setup_probe'
assert_contains "$command_log" 'xcb_setup_probe'
for artifact in \
  milestone1-runtime-test.log \
  milestone1-meson-test.log \
  milestone1-xcb-probe.log \
  milestone1-journal.log \
  milestone1-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone1 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone1-summary.json" '"sanitizer": "passed"'
assert_contains "$artifact_dir/milestone1-summary.json" '"xorg_xwayland_absent": true'

run_failure "$work_dir/milestone1-bad-facts.out" \
  env GW_VM_TEST_BAD_FACTS=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone1-summary.json" '"evidence_errors"'

run_failure "$work_dir/milestone1-empty-journal.out" \
  env GW_VM_TEST_EMPTY_JOURNAL=1 "$gw_vm" milestone1-runtime-test --yes
assert_contains "$artifact_dir/milestone1-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone1-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone1-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone1-error.out" \
  env GW_VM_TEST_FAIL_MATCH='systemd-run --unit="$unit"' "$gw_vm" milestone1-runtime-test --yes
assert_contains "$work_dir/milestone1-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone1-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone1-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone2-gate.out" "$gw_vm" milestone2-runtime-test
assert_contains "$work_dir/milestone2-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone2-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-bad-base.out" \
  'HEAD is not based on required Milestone 2 commit 4e219a8093c2b79857efc046c3bf0948cc7704f8'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone2-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone2.out" "$gw_vm" milestone2-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'unit=glasswyrmd-m2.service'
assert_contains "$command_log" 'build_dir=/var/tmp/glasswyrm-build-m2'
assert_contains "$command_log" 'sanitizer_build_dir=/var/tmp/glasswyrm-build-m2-asan'
assert_contains "$command_log" 'artifact_dir=$2'
assert_contains "$command_log" 'portageq match / "$1"'
assert_contains "$command_log" '_SYSTEMD_INVOCATION_ID=$unit_invocation_id'
assert_contains "$command_log" '"$setup_probe" --display :99 --byte-order little'
assert_contains "$command_log" '"$setup_probe" --display :99 --byte-order big'
assert_contains "$command_log" '"$m2_probe" --display :99 --byte-order little --basic'
assert_contains "$command_log" '"$m2_probe" --display :99 --byte-order big --basic'
assert_contains "$command_log" '"$m2_probe" --display :99 --errors'
assert_contains "$command_log" '"$m2_probe" --display :99 --cleanup'
assert_contains "$command_log" '"$m2_probe" --display :99 --cross-endian'
assert_contains "$command_log" 'DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_setup_probe"'
assert_contains "$command_log" 'DISPLAY=:99 XAUTHORITY=/dev/null "$xcb_m2_probe"'
for artifact in \
  milestone2-runtime-test.log \
  milestone2-meson-test.log \
  milestone2-raw-probe.log \
  milestone2-xcb-probe.log \
  milestone2-journal.log \
  milestone2-facts.env \
  milestone2-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone2 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone2-summary.json" \
  '"required_base_commit": "4e219a8093c2b79857efc046c3bf0948cc7704f8"'
assert_contains "$artifact_dir/milestone2-summary.json" '"m1_regression_tests": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"cross_endian_property": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"xcb_m2": "passed"'
assert_contains "$artifact_dir/milestone2-summary.json" '"xorg_xwayland_absent": true'

run_failure "$work_dir/milestone2-bad-facts.out" \
  env GW_VM_TEST_BAD_M2_FACTS=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone2-summary.json" '"evidence_errors"'
assert_contains "$artifact_dir/milestone2-summary.json" 'resource_cleanup must be passed'

run_failure "$work_dir/milestone2-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M2_JOURNAL=1 "$gw_vm" milestone2-runtime-test --yes
assert_contains "$artifact_dir/milestone2-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone2-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone2-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone2-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=glasswyrmd-m2.service' "$gw_vm" milestone2-runtime-test --yes
assert_contains "$work_dir/milestone2-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone2-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone2-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone3-gate.out" "$gw_vm" milestone3-runtime-test
assert_contains "$work_dir/milestone3-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone3-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-bad-base.out" \
  'HEAD is not based on required Milestone 3 commit d6816484f0293b8b47edf0f13e5da691014e3e7c'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone3-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_success "$work_dir/milestone3.out" "$gw_vm" milestone3-runtime-test --yes
assert_contains "$command_log" '.glasswyrm-vm-source'
assert_contains "$command_log" 'build_dir=/var/tmp/glasswyrm-build-m3'
assert_contains "$command_log" 'sanitizer_build_dir=/var/tmp/glasswyrm-build-m3-asan'
assert_contains "$command_log" 'ipc_build_dir=/var/tmp/glasswyrm-build-m3-ipc-only'
assert_contains "$command_log" 'install_root=/var/tmp/glasswyrm-m3-install'
assert_contains "$command_log" 'unit=gwipc-m3.service'
assert_contains "$command_log" '--property=RuntimeDirectory=glasswyrm-m3'
assert_contains "$command_log" '-Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false'
assert_contains "$command_log" 'DESTDIR="$install_root" meson install'
for mode in roundtrip ping-pong contract-roundtrip fd-transfer snapshot \
  incompatible-version wrong-role missing-capability malformed-envelope \
  sequence-violation limit; do
  assert_contains "$command_log" "run_client $mode"
done
for artifact in \
  milestone3-runtime-test.log \
  milestone3-meson-test.log \
  milestone3-install-test.log \
  milestone3-handshake.log \
  milestone3-fd-transfer.log \
  milestone3-snapshot.log \
  milestone3-malformed.log \
  milestone3-journal.log \
  milestone3-facts.env \
  milestone3-summary.json; do
  [[ -f $artifact_dir/$artifact ]] || fail "milestone3 scenario did not create $artifact"
done
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone3-summary.json" \
  '"required_base_commit": "d6816484f0293b8b47edf0f13e5da691014e3e7c"'
assert_contains "$artifact_dir/milestone3-summary.json" '"api_version": "0.1.0"'
assert_contains "$artifact_dir/milestone3-summary.json" '"wire_version": "1.0"'
assert_contains "$artifact_dir/milestone3-summary.json" '"contract_roundtrip": "passed"'
assert_contains "$artifact_dir/milestone3-summary.json" '"xorg_xwayland_absent": true'

run_success "$work_dir/milestone3-newer-api.out" \
  env GW_VM_TEST_M3_API_VERSION=0.3.0 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone3-summary.json" '"api_version": "0.3.0"'

run_failure "$work_dir/milestone3-bad-facts.out" \
  env GW_VM_TEST_BAD_M3_FACTS=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": false'
assert_contains "$artifact_dir/milestone3-summary.json" 'sequence_isolation must be passed'

run_failure "$work_dir/milestone3-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M3_JOURNAL=1 "$gw_vm" milestone3-runtime-test --yes
assert_contains "$artifact_dir/milestone3-summary.json" 'current invocation journal is missing or empty'

run_success "$work_dir/milestone3-wrapper.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone3-runtime-test.sh" --yes

: >"$command_log"
run_failure "$work_dir/milestone3-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=gwipc-m3.service' "$gw_vm" milestone3-runtime-test --yes
assert_contains "$work_dir/milestone3-error.out" 'failed during: guest-runtime'
assert_contains "$artifact_dir/milestone3-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone3-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone4-gate.out" "$gw_vm" milestone4-runtime-test
assert_contains "$work_dir/milestone4-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone4-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone4-runtime-test --yes
assert_contains "$work_dir/milestone4-bad-base.out" \
  'HEAD is not based on required Milestone 4 commit 6080e094c35929d0fb2deb4b31ff4040e392a75e'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone4-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" scenario milestone4-runtime-test --yes
assert_contains "$work_dir/milestone4-dirty-source.out" 'requires committed source outside Plans/'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

run_failure "$work_dir/milestone4-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone4-runtime-test.sh"
assert_contains "$work_dir/milestone4-wrapper-gate.out" '--yes'

: >"$command_log"
run_failure "$work_dir/milestone5-gate.out" "$gw_vm" milestone5-runtime-test
assert_contains "$work_dir/milestone5-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone5-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-bad-base.out" \
  'HEAD is not based on required Milestone 5 commit b27a19a869de1d950566b1e3fb9a661e22d5642f'

: >"$command_log"
run_failure "$work_dir/milestone5-dirty-source.out" \
  env GW_VM_TEST_GIT_DIRTY=1 "$gw_vm" scenario milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-dirty-source.out" 'requires committed source outside Plans/'

run_failure "$work_dir/milestone5-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone5-runtime-test.sh"
assert_contains "$work_dir/milestone5-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone5.out" "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" '"passed": true'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m5-gwm'
assert_contains "$command_log" 'RuntimeDirectory=glasswyrm-m5'
assert_contains "$command_log" 'run_producer snapshot-reconnect'
assert_contains "$command_log" 'gwipc_policy_c_consumer.c'
assert_file_glob "$artifact_dir/milestone5-policies.tar"

: >"$command_log"
run_failure "$work_dir/milestone5-bad-facts.out" \
  env GW_VM_TEST_BAD_M5_FACTS=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" '"passed": false'
assert_contains "$command_log" 'milestone5-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone5-empty-journal.out" \
  env GW_VM_TEST_EMPTY_M5_JOURNAL=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" 'current invocation journal is missing or empty'

: >"$command_log"
run_failure "$work_dir/milestone5-old-api.out" \
  env GW_VM_TEST_BAD_M5_API=1 "$gw_vm" milestone5-runtime-test --yes
assert_contains "$artifact_dir/milestone5-summary.json" 'api_version must be at least 0.3.0'

: >"$command_log"
run_failure "$work_dir/milestone5-error.out" \
  env GW_VM_TEST_FAIL_MATCH='unit=gwm-m5.service' "$gw_vm" milestone5-runtime-test --yes
assert_contains "$work_dir/milestone5-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone5-journal.log'

: >"$command_log"
run_failure "$work_dir/milestone6-gate.out" "$gw_vm" milestone6-runtime-test
assert_contains "$work_dir/milestone6-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone6-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone6-runtime-test --yes
assert_contains "$work_dir/milestone6-bad-base.out" \
  'HEAD is not based on required Milestone 6 commit 9b9170de569fa112c400780beec3140bd4ef6af1'

run_failure "$work_dir/milestone6-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone6-runtime-test.sh"
assert_contains "$work_dir/milestone6-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone6.out" "$gw_vm" scenario milestone6-runtime-test --yes
assert_contains "$artifact_dir/milestone6-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone6-summary.json" \
  '"required_base_commit": "9b9170de569fa112c400780beec3140bd4ef6af1"'
assert_contains "$artifact_dir/milestone6-summary.json" '"restart_probe": "passed"'
assert_contains "$artifact_dir/milestone6-summary.json" '"xcb_m6_probe": "passed"'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-runtime'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-server'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m6-server-ipc'
assert_contains "$command_log" 'gwipc_lifecycle_c_consumer.c'
assert_contains "$command_log" 'glasswyrmd-integrated-lifecycle'
assert_contains "$command_log" 'm6_restart_hold_probe'
assert_contains "$command_log" 'systemctl restart gwm-m6.service'
assert_contains "$command_log" 'systemctl restart gwcomp-m6.service'
assert_file_glob "$artifact_dir/milestone6-lifecycle.tar"

: >"$command_log"
run_failure "$work_dir/milestone6-bad-restart.out" \
  env GW_VM_TEST_BAD_M6_FACTS=1 "$gw_vm" milestone6-runtime-test --yes
assert_contains "$artifact_dir/milestone6-summary.json" \
  'restart_probe must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-gate.out" "$gw_vm" milestone7-runtime-test
assert_contains "$work_dir/milestone7-gate.out" '--yes'
assert_not_contains "$command_log" '.glasswyrm-vm-source'

: >"$command_log"
run_failure "$work_dir/milestone7-bad-base.out" \
  env GW_VM_TEST_BAD_BASE=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$work_dir/milestone7-bad-base.out" \
  'HEAD is not based on required Milestone 7 commit d05dcf2bb979fd82dd5a1dd0a07e34a915ec9746'

run_failure "$work_dir/milestone7-wrapper-gate.out" \
  "$repo_root/tools/gw-vm.d/scenarios/milestone7-runtime-test.sh"
assert_contains "$work_dir/milestone7-wrapper-gate.out" '--yes'

: >"$command_log"
run_success "$work_dir/milestone7.out" "$gw_vm" scenario milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" '"passed": true'
assert_contains "$artifact_dir/milestone7-summary.json" \
  '"required_base_commit": "d05dcf2bb979fd82dd5a1dd0a07e34a915ec9746"'
assert_contains "$artifact_dir/milestone7-summary.json" '"final_frame_golden": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"connection_survival": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"replay_hash": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"post_restart_hash": "passed"'
assert_contains "$artifact_dir/milestone7-summary.json" '"compiler_c": "gcc test"'
assert_contains "$artifact_dir/milestone7-summary.json" '"scene_manifest": "absent"'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-runtime'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-server'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-server-ipc'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-gwm'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-gwcomp'
assert_contains "$command_log" '/var/tmp/glasswyrm-build-m7-ipc-only'
assert_contains "$command_log" '/run/glasswyrm-m7-gwm/gwm.sock'
assert_contains "$command_log" '/run/glasswyrm-m7-gwcomp/gwcomp.sock'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-dumps'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-scenes'
assert_contains "$command_log" '/var/tmp/glasswyrm-m7-control'
assert_contains "$command_log" 'PrivateTmp=yes'
assert_contains "$command_log" 'BindReadOnlyPaths="$runtime_build_dir"'
assert_contains "$command_log" 'BindPaths="$dump_dir"'
assert_contains "$command_log" 'BindPaths="$scene_dir"'
assert_contains "$command_log" '--software-content'
assert_contains "$command_log" 'x11_milestone6_probe'
assert_contains "$command_log" 'gwcomp-golden'
assert_contains "$command_log" 'wm-policy'
assert_contains "$command_log" 'gwipc-malformed'
assert_contains "$command_log" 'x11_milestone7_probe'
assert_contains "$command_log" 'xcb_milestone7_probe'
assert_contains "$command_log" 'm7_restart_hold_probe'
assert_contains "$command_log" 'systemctl restart gwcomp-m7.service'
assert_contains "$command_log" 'systemctl restart gwm-m7.service'
assert_contains "$command_log" 'frame-hashes.json'
assert_contains "$command_log" 'post_restart_frame=$((pre_continue_count + 1))'
assert_contains "$command_log" 'frame_field_at "$post_restart_frame" file'
assert_contains "$command_log" 'cmp "$final_ppm" "$source_dir/tests/fixtures/m7/final.ppm"'
assert_contains "$command_log" 'cmp "$restart_result"'
assert_contains "$command_log" 'journalctl -u glasswyrmd-m7.service -u gwcomp-m7.service'
assert_contains "$command_log" 'archive_files+=(scene.jsonl)'
assert_contains "$command_log" 'scene_manifest=absent'
assert_contains "$command_log" 'milestone7-rendering.tar'
assert_file_glob "$artifact_dir/milestone7-rendering.tar"
if tar -tf "$artifact_dir/milestone7-rendering.tar" | grep -Fx scene.jsonl >/dev/null; then
  fail 'M7 fake rendering archive unexpectedly contains optional scene.jsonl'
fi

: >"$command_log"
run_failure "$work_dir/milestone7-bad-golden.out" \
  env GW_VM_TEST_BAD_M7_FACTS=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" \
  'final_frame_golden must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-bad-replay.out" \
  env GW_VM_TEST_BAD_M7_REPLAY=1 "$gw_vm" milestone7-runtime-test --yes
assert_contains "$artifact_dir/milestone7-summary.json" \
  'replay_hash must be passed'

: >"$command_log"
run_failure "$work_dir/milestone7-error.out" \
  env GW_VM_TEST_FAIL_MATCH='--software-content' "$gw_vm" milestone7-runtime-test --yes
assert_contains "$work_dir/milestone7-error.out" 'failed during: guest-runtime'
assert_contains "$command_log" 'milestone7-glasswyrmd-journal.log'

printf 'gw-vm CLI tests passed\n'
