#!/usr/bin/env bash

if [[ -n "${GW_VM_MILESTONE5_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE5_LOADED=1

M5_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m5-artifacts
M5_REQUIRED_BASE_COMMIT=b27a19a869de1d950566b1e3fb9a661e22d5642f
M5_TESTED_COMMIT=
M5_ARTIFACT_NAMES=(
  milestone5-runtime-test.log milestone5-meson-test.log
  milestone5-producer.log milestone5-golden-test.log
  milestone5-malformed.log
  milestone5-journal.log milestone5-facts.env
)

milestone5_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1
artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m5
sanitizer_build_dir=/var/tmp/glasswyrm-build-m5-asan
gwm_build_dir=/var/tmp/glasswyrm-build-m5-gwm
ipc_build_dir=/var/tmp/glasswyrm-build-m5-ipc-only
install_root=/var/tmp/glasswyrm-m5-install
policy_dir=/var/tmp/glasswyrm-m5-policies
unit=gwm-m5.service
socket_path=/run/glasswyrm-m5/gwm.sock
runtime_log="$artifact_dir/milestone5-runtime-test.log"
meson_log="$artifact_dir/milestone5-meson-test.log"
producer_log="$artifact_dir/milestone5-producer.log"
golden_log="$artifact_dir/milestone5-golden-test.log"
malformed_log="$artifact_dir/milestone5-malformed.log"
journal_log="$artifact_dir/milestone5-journal.log"
facts="$artifact_dir/milestone5-facts.env"
failure_stage=dependency-preparation
full_tests=not-run sanitizer=not-run gwm_only=not-run ipc_only=not-run
legacy_consumers=not-run policy_consumers=not-run basic=not-run snapshot_order=not-run
transient=not-run override_redirect=not-run focus=not-run stacking=not-run fullscreen=not-run
maximize_minimize=not-run incremental_update=not-run invalid_context_isolation=not-run
invalid_window_isolation=not-run unknown_reference_recovery=not-run cycle_rejection=not-run
snapshot_abort=not-run malformed_peer_isolation=not-run reconnect_equality=not-run golden_hash_count=0
policy_archive=not-run systemd_runtime=not-run socket_cleanup=not-run
unit_invocation_id=

package_installed() { [[ -n "$(portageq match / "$1" 2>/dev/null)" ]]; }
mkdir -p "$artifact_dir"
rm -rf "$policy_dir"
mkdir -p "$policy_dir"
rm -f "$artifact_dir"/milestone5-* "$facts"
touch "$runtime_log" "$meson_log" "$producer_log" "$golden_log" \
  "$malformed_log" "$journal_log"
exec > >(tee -a "$runtime_log") 2>&1

record_facts() {
  local status=$? journal_status=0 api_version=unknown wire_version=unknown soversion=unknown
  set +e
  systemctl stop "$unit" >/dev/null 2>&1
  if [[ -n "$unit_invocation_id" ]]; then
    journalctl "_SYSTEMD_INVOCATION_ID=$unit_invocation_id" --no-pager >"$journal_log" 2>&1 || journal_status=$?
    [[ -s "$journal_log" ]] || journal_status=1
  else
    echo 'systemd invocation ID was not recorded' >"$journal_log"; journal_status=1
  fi
  systemctl reset-failed "$unit" >/dev/null 2>&1 || true
  systemctl daemon-reload >/dev/null 2>&1 || true
  if ((status == 0 && journal_status != 0)); then status=$journal_status; failure_stage=journal-collection; fi
  if [[ -x "$ipc_build_dir/tests/gwipc_wire_probe" ]]; then
    api_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-api-version 2>/dev/null || printf unknown)"
    wire_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-wire-version 2>/dev/null || printf unknown)"
  fi
  if [[ -e "$install_root/usr/lib64/libgwipc.so.0" || -e "$install_root/usr/lib/libgwipc.so.0" ]]; then
    soversion=0
  fi
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$status"
    printf 'compiler_c=%s\n' "$(cc --version 2>/dev/null | head -n1 || printf unavailable)"
    printf 'compiler_cxx=%s\n' "$(c++ --version 2>/dev/null | head -n1 || printf unavailable)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' "$(meson --version)" "$(ninja --version)" "$(systemctl --version | head -n1)"
    printf 'api_version=%s\nsoversion=%s\nwire_version=%s\n' "$api_version" "$soversion" "$wire_version"
    if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then echo x_servers_absent=false; else echo x_servers_absent=true; fi
    for key in full_tests sanitizer gwm_only ipc_only legacy_consumers policy_consumers basic snapshot_order transient override_redirect focus stacking fullscreen maximize_minimize incremental_update invalid_context_isolation invalid_window_isolation unknown_reference_recovery cycle_rejection snapshot_abort malformed_peer_isolation reconnect_equality golden_hash_count policy_archive systemd_runtime socket_cleanup; do
      printf '%s=%s\n' "$key" "${!key}"
    done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT

[[ -f "$source_dir/.glasswyrm-vm-source" ]]
if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then
  echo 'Milestone 5 requires a guest without Xorg or Xwayland installed' >&2; exit 1
fi
export NOCOLOR=1
emerge --verbose --noreplace --color=n sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig x11-libs/libxcb x11-base/xcb-proto

failure_stage=full-build-and-test
rm -rf "$build_dir" "$sanitizer_build_dir" "$gwm_build_dir" "$ipc_build_dir" "$install_root"
{ meson setup "$build_dir" "$source_dir" -Dwerror=true; meson compile -C "$build_dir"; meson test -C "$build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
full_tests=passed
failure_stage=sanitizer-build-and-test
probe=/var/tmp/glasswyrm-m5-sanitizer-probe
if printf 'int main(void){return 0;}\n' | cc -x c - -fsanitize=address,undefined -o "$probe" >>"$meson_log" 2>&1 && "$probe"; then
  { meson setup "$sanitizer_build_dir" "$source_dir" -Dwerror=true -Dasan=true -Dubsan=true; meson compile -C "$sanitizer_build_dir"; meson test -C "$sanitizer_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
  sanitizer=passed
else sanitizer=unavailable; fi
rm -f "$probe"
failure_stage=gwm-only-build-and-test
{ meson setup "$gwm_build_dir" "$source_dir" -Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=true -Dgwcomp=false -Dtools=false; meson compile -C "$gwm_build_dir"; meson test -C "$gwm_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
gwm_only=passed
failure_stage=ipc-only-regression
{ meson setup "$ipc_build_dir" "$source_dir" --prefix=/usr -Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false; meson compile -C "$ipc_build_dir"; meson test -C "$ipc_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
ipc_only=passed
failure_stage=staged-install-and-consumers
DESTDIR="$install_root" meson install -C "$ipc_build_dir" >>"$meson_log" 2>&1
staged_lib_dir="$install_root/usr/lib64"; [[ -d "$staged_lib_dir" ]] || staged_lib_dir="$install_root/usr/lib"
export PKG_CONFIG_PATH="$staged_lib_dir/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$install_root" LD_LIBRARY_PATH="$staged_lib_dir"
read -r -a pkg_config_flags <<<"$(pkg-config --cflags --libs gwipc)"
printf 'staged gwipc flags: %s\n' "${pkg_config_flags[*]}" | tee -a "$meson_log"
cc "$source_dir/tests/install/gwipc_c_consumer.c" -o /var/tmp/gwipc-m4-c-consumer "${pkg_config_flags[@]}"
c++ -std=c++20 "$source_dir/tests/install/gwipc_cpp_consumer.cpp" -o /var/tmp/gwipc-m4-cpp-consumer "${pkg_config_flags[@]}"
/var/tmp/gwipc-m4-c-consumer; /var/tmp/gwipc-m4-cpp-consumer
legacy_consumers=passed
cc "$source_dir/tests/install/gwipc_policy_c_consumer.c" -o /var/tmp/gwipc-m5-policy-c "${pkg_config_flags[@]}"
c++ -std=c++20 "$source_dir/tests/install/gwipc_policy_cpp_consumer.cpp" -o /var/tmp/gwipc-m5-policy-cpp "${pkg_config_flags[@]}"
/var/tmp/gwipc-m5-policy-c; /var/tmp/gwipc-m5-policy-cpp
policy_consumers=passed

gwm="$gwm_build_dir/src/gwm"
producer="$build_dir/src/gwm_m5_producer"
malformed="$build_dir/tests/gwipc_probe_client"
test -x "$gwm"; test -x "$producer"; test -x "$malformed"
failure_stage=systemd-runtime
[[ ! -e "$socket_path" ]]
systemctl stop "$unit" >/dev/null 2>&1 || true
systemctl reset-failed "$unit" >/dev/null 2>&1 || true
systemctl daemon-reload
systemd-run --unit="$unit" --property=Type=exec --property=NoNewPrivileges=yes \
  --property=PrivateDevices=yes --property=PrivateTmp=yes \
  --property="BindReadOnlyPaths=$gwm_build_dir" --property="BindPaths=$policy_dir" \
  --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet= \
  --property=AmbientCapabilities= --property=Restart=no \
  --property=RuntimeDirectory=glasswyrm-m5 --property=RuntimeDirectoryMode=0700 \
  "$gwm" --ipc-socket "$socket_path"
unit_invocation_id="$(systemctl show "$unit" -p InvocationID --value)"
[[ "$unit_invocation_id" =~ ^[0-9a-f]{32}$ ]]
for _ in {1..100}; do [[ -S "$socket_path" ]] && break; sleep .1; done
[[ -S "$socket_path" ]]; [[ "$(stat -c %a "$socket_path")" == 600 ]]; [[ "$(stat -c %u "$socket_path")" == "$(id -u)" ]]
run_producer() { "$producer" --socket "$socket_path" --scenario "$1" --output "$policy_dir/$1.json" 2>&1 | tee -a "$2"; }
run_producer basic "$producer_log"; basic=passed
run_producer snapshot-order "$producer_log"; snapshot_order=passed
for scenario in transient override-redirect focus stacking fullscreen maximize-minimize incremental-update; do run_producer "$scenario" "$producer_log"; done
transient=passed; override_redirect=passed; focus=passed; stacking=passed; fullscreen=passed; maximize_minimize=passed; incremental_update=passed
run_producer invalid-context "$producer_log"; systemctl is-active --quiet "$unit"; invalid_context_isolation=passed
run_producer invalid-window "$producer_log"; invalid_window_isolation=passed
run_producer unknown-reference "$producer_log"; unknown_reference_recovery=passed
run_producer transient-cycle "$producer_log"; cycle_rejection=passed
run_producer snapshot-abort "$producer_log"; snapshot_abort=passed
"$malformed" --socket "$socket_path" --mode malformed-envelope 2>&1 | tee -a "$malformed_log"; systemctl is-active --quiet "$unit"; malformed_peer_isolation=passed
run_producer basic "$producer_log"
run_producer snapshot-reconnect "$producer_log"; reconnect_equality=passed
failure_stage=golden-validation
(cd "$policy_dir" && sha256sum -c "$source_dir/tests/fixtures/m5/SHA256SUMS") 2>&1 | tee -a "$golden_log"
golden_hash_count="$(wc -l <"$source_dir/tests/fixtures/m5/SHA256SUMS")"
(cd "$policy_dir" && sha256sum -- *.json >SHA256SUMS && tar -cf "$artifact_dir/milestone5-policies.tar" -- *.json SHA256SUMS)
tar -tf "$artifact_dir/milestone5-policies.tar" | grep -Fx basic.json; policy_archive=passed
systemctl stop "$unit"; systemctl show "$unit" -p Result --value | grep -Fx success
systemd_runtime=passed; [[ ! -e "$socket_path" ]]; socket_cleanup=passed
failure_stage=
GUEST_SCRIPT
}

collect_milestone5_artifacts() {
  local name path failed=0
  init_artifacts
  for name in "${M5_ARTIFACT_NAMES[@]}"; do
    path="$ARTIFACTS_PATH_ABS/$name"
    guest_run_script 'set -euo pipefail; cat "$1"' "$M5_GUEST_ARTIFACT_DIR/$name" >"$path" 2>&1 || { echo "Unable to collect guest artifact: $name" >>"$path"; failed=1; }
  done
  ssh_arguments
  scp -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10 \
    "$SSH_TARGET:$M5_GUEST_ARTIFACT_DIR/milestone5-policies.tar" \
    "$ARTIFACTS_PATH_ABS/milestone5-policies.tar" || failed=1
  if [[ -f "$ARTIFACTS_PATH_ABS/milestone5-policies.tar" ]]; then tar -tf "$ARTIFACTS_PATH_ABS/milestone5-policies.tar" >/dev/null || failed=1; fi
  return "$failed"
}

write_milestone5_summary() {
  local passed=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone5-facts.env" summary="$ARTIFACTS_PATH_ABS/milestone5-summary.json"
  local api_version=unknown api_compatible=false
  if [[ -f "$facts" ]]; then api_version="$(sed -n 's/^api_version=//p' "$facts" | tail -n 1)"; fi
  if semantic_version_at_least "$api_version" 0.3.0; then api_compatible=true; fi
  python3 - "$facts" "$summary" "$passed" "$failure" "$M5_REQUIRED_BASE_COMMIT" "${M5_TESTED_COMMIT:-unknown}" "$api_compatible" <<'PY'
import json, pathlib, sys
facts_path, out, requested, failure, base, tested, api_compatible = sys.argv[1:]
facts = {}
p = pathlib.Path(facts_path)
if p.is_file():
    for line in p.read_text(errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep and key.replace("_", "").isalnum(): facts[key] = value
required = {key: "passed" for key in ("full_tests gwm_only ipc_only legacy_consumers policy_consumers basic snapshot_order transient override_redirect focus stacking fullscreen maximize_minimize incremental_update invalid_context_isolation invalid_window_isolation unknown_reference_recovery cycle_rejection snapshot_abort malformed_peer_isolation reconnect_equality policy_archive systemd_runtime socket_cleanup".split())}
required.update(scenario_exit="0", x_servers_absent="true", soversion="0", wire_version="1.0")
errors = [f"{k} must be {v}" for k,v in required.items() if facts.get(k) != v]
if api_compatible != "true": errors.append("api_version must be at least 0.3.0 with major 0")
elif not facts.get("api_version", "").startswith("0."): errors.append("api_version major must be 0")
if facts.get("sanitizer") not in {"passed", "unavailable"}: errors.append("sanitizer must be passed or unavailable")
try:
    if int(facts.get("golden_hash_count", "0")) < 1: errors.append("golden_hash_count must be positive")
except ValueError: errors.append("golden_hash_count must be numeric")
journal = p.with_name("milestone5-journal.log")
if not journal.is_file() or not journal.stat().st_size: errors.append("current invocation journal is missing or empty")
payload={"required_base_commit":base,"tested_commit":tested,"api_version":facts.get("api_version","unknown"),"soversion":facts.get("soversion","unknown"),"wire_version":facts.get("wire_version","unknown"),"xorg_xwayland_absent":facts.get("x_servers_absent")=="true","results":{k:facts.get(k,"unknown") for k in required if k not in {"scenario_exit","x_servers_absent","soversion","wire_version"}},"sanitizer":facts.get("sanitizer","unknown"),"golden_hash_count":facts.get("golden_hash_count","0"),"journal":"passed" if journal.is_file() and journal.stat().st_size else "failed","passed":requested=="true" and not errors,"failure_stage":failure or facts.get("failure_stage",""),"evidence_errors":errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+"\n")
if requested=="true" and errors: raise SystemExit(2)
PY
}

verify_milestone5_source_identity() {
  local status unexpected='' line
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do [[ -z "$line" || "$line" == '?? Plans/'* ]] || unexpected+="${unexpected:+$'\n'}$line"; done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 5 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  local current; current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M5_TESTED_COMMIT" || "$current" == "$M5_TESTED_COMMIT" ]] || { echo 'Milestone 5 source commit changed during acceptance.' >&2; return 1; }
}

prepare_milestone5_evidence() {
  M5_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone5_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M5_REQUIRED_BASE_COMMIT" "$M5_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 5 commit %s\n' "$M5_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone5-*
}

clear_milestone5_guest_artifacts() {
  guest_run_script 'set -euo pipefail; artifact_dir=$1; [[ "$artifact_dir" == /var/tmp/glasswyrm-m5-artifacts ]]; rm -rf -- "$artifact_dir"; mkdir -p -- "$artifact_dir"' "$M5_GUEST_ARTIFACT_DIR"
}

milestone5_runtime_test() {
  local approved=$1 failure='' status=0 collection_status=0 script current artifacts_prepared=false
  require_approval milestone5-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  if prepare_milestone5_evidence; then :; else status=$?; failure=source-evidence; fi
  if [[ -z "$failure" ]]; then if vm_boot; then :; else status=$?; failure=boot; fi; fi
  if [[ -z "$failure" ]]; then if clear_milestone5_guest_artifacts; then artifacts_prepared=true; else status=$?; failure=artifact-preparation; fi; fi
  if [[ -z "$failure" ]]; then if verify_milestone5_source_identity && push_source && verify_milestone5_source_identity; then :; else status=$?; failure=push-source; fi; fi
  if [[ -z "$failure" ]]; then script="$(milestone5_guest_script)"; if capture_guest_action milestone5-runtime-test "$ARTIFACTS_PATH_ABS/milestone5-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M5_GUEST_ARTIFACT_DIR"; then :; else status=$?; failure=guest-runtime; fi; fi
  if [[ "$artifacts_prepared" == true ]]; then collect_milestone5_artifacts || collection_status=$?; fi
  if ((collection_status != 0)) && [[ -z "$failure" ]]; then status=$collection_status; failure=artifact-collection; fi
  if [[ -n "$failure" ]]; then write_milestone5_summary false "$failure" || true; printf 'Milestone 5 VM runtime test failed during: %s\n' "$failure" >&2; print_artifacts >&2; return "${status:-1}"; fi
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  if [[ "$current" != "$M5_TESTED_COMMIT" ]] || ! verify_milestone5_source_identity; then write_milestone5_summary false source-identity-changed; return 1; fi
  write_milestone5_summary true ''; echo 'Milestone 5 VM runtime test passed.'; print_artifacts
}
