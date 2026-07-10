#!/usr/bin/env bash

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
  *' merge-base --is-ancestor '*) exit 0 ;;
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
for command in doctor status reset pretend emerge unmerge narrow-test collect full-packaging-test push-source milestone1-runtime-test; do
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

printf 'gw-vm CLI tests passed\n'
