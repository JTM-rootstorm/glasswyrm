#!/usr/bin/env bash

if [[ -n "${GW_VM_MILESTONE4_LOADED:-}" ]]; then return 0; fi
GW_VM_MILESTONE4_LOADED=1

M4_GUEST_ARTIFACT_DIR=/var/tmp/glasswyrm-m4-artifacts
M4_REQUIRED_BASE_COMMIT=6080e094c35929d0fb2deb4b31ff4040e392a75e
M4_TESTED_COMMIT=
M4_ARTIFACT_NAMES=(
  milestone4-runtime-test.log milestone4-meson-test.log
  milestone4-producer.log milestone4-golden-test.log
  milestone4-buffer-release.log milestone4-malformed.log
  milestone4-journal.log milestone4-facts.env
)

milestone4_guest_script() {
  cat <<'GUEST_SCRIPT'
set -euo pipefail
source_dir=$1
artifact_dir=$2
build_dir=/var/tmp/glasswyrm-build-m4
sanitizer_build_dir=/var/tmp/glasswyrm-build-m4-asan
gwcomp_build_dir=/var/tmp/glasswyrm-build-m4-gwcomp
ipc_build_dir=/var/tmp/glasswyrm-build-m4-ipc-only
install_root=/var/tmp/glasswyrm-m4-install
frame_dir=/var/tmp/glasswyrm-m4-frames
unit=gwcomp-m4.service
socket_path=/run/glasswyrm-m4/gwcomp.sock
runtime_log="$artifact_dir/milestone4-runtime-test.log"
meson_log="$artifact_dir/milestone4-meson-test.log"
producer_log="$artifact_dir/milestone4-producer.log"
golden_log="$artifact_dir/milestone4-golden-test.log"
release_log="$artifact_dir/milestone4-buffer-release.log"
malformed_log="$artifact_dir/milestone4-malformed.log"
journal_log="$artifact_dir/milestone4-journal.log"
facts="$artifact_dir/milestone4-facts.env"
failure_stage=dependency-preparation
full_tests=not-run sanitizer=not-run compositor_only=not-run ipc_only=not-run
typed_consumers=not-run basic_frame=not-run damage_frame=not-run stacking=not-run
visibility=not-run clipping=not-run opacity=not-run buffer_release=not-run
detach_remove=not-run unknown_reference=not-run
invalid_metadata_isolation=not-run invalid_buffer_isolation=not-run
malformed_peer_isolation=not-run reconnect_snapshot=not-run golden_hash_count=0
frame_archive=not-run systemd_runtime=not-run socket_cleanup=not-run
unit_invocation_id=

package_installed() { [[ -n "$(portageq match / "$1" 2>/dev/null)" ]]; }
mkdir -p "$artifact_dir"
rm -rf "$frame_dir"
mkdir -p "$frame_dir"
rm -f "$artifact_dir"/milestone4-* "$facts"
touch "$runtime_log" "$meson_log" "$producer_log" "$golden_log" \
  "$release_log" "$malformed_log" "$journal_log"
exec > >(tee -a "$runtime_log") 2>&1

record_facts() {
  local status=$? journal_status=0 api_version=unknown wire_version=unknown
  set +e
  systemctl stop "$unit" >/dev/null 2>&1
  if [[ -n "$unit_invocation_id" ]]; then
    journalctl "_SYSTEMD_INVOCATION_ID=$unit_invocation_id" --no-pager >"$journal_log" 2>&1 || journal_status=$?
    [[ -s "$journal_log" ]] || journal_status=1
  else
    echo 'systemd invocation ID was not recorded' >"$journal_log"; journal_status=1
  fi
  if ((status == 0 && journal_status != 0)); then status=$journal_status; failure_stage=journal-collection; fi
  if [[ -x "$ipc_build_dir/tests/gwipc_wire_probe" ]]; then
    api_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-api-version 2>/dev/null || printf unknown)"
    wire_version="$("$ipc_build_dir/tests/gwipc_wire_probe" --print-wire-version 2>/dev/null || printf unknown)"
  fi
  {
    printf 'failure_stage=%s\nscenario_exit=%s\n' "$failure_stage" "$status"
    printf 'compiler_c=%s\n' "$(cc --version 2>/dev/null | head -n1 || printf unavailable)"
    printf 'compiler_cxx=%s\n' "$(c++ --version 2>/dev/null | head -n1 || printf unavailable)"
    printf 'meson_version=%s\nninja_version=%s\nsystemd_version=%s\n' "$(meson --version)" "$(ninja --version)" "$(systemctl --version | head -n1)"
    printf 'api_version=%s\nwire_version=%s\n' "$api_version" "$wire_version"
    if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then echo x_servers_absent=false; else echo x_servers_absent=true; fi
    for key in full_tests sanitizer compositor_only ipc_only typed_consumers basic_frame damage_frame stacking visibility clipping opacity buffer_release detach_remove unknown_reference invalid_metadata_isolation invalid_buffer_isolation malformed_peer_isolation reconnect_snapshot golden_hash_count frame_archive systemd_runtime socket_cleanup; do
      printf '%s=%s\n' "$key" "${!key}"
    done
  } >"$facts"
  exit "$status"
}
trap record_facts EXIT

[[ -f "$source_dir/.glasswyrm-vm-source" ]]
if package_installed x11-base/xorg-server || package_installed x11-base/xwayland; then
  echo 'Milestone 4 requires a guest without Xorg or Xwayland installed' >&2; exit 1
fi
export NOCOLOR=1
emerge --verbose --noreplace --color=n sys-devel/gcc dev-build/meson dev-build/ninja virtual/pkgconfig x11-libs/libxcb x11-base/xcb-proto

failure_stage=full-build-and-test
rm -rf "$build_dir" "$sanitizer_build_dir" "$gwcomp_build_dir" "$ipc_build_dir" "$install_root"
{ meson setup "$build_dir" "$source_dir" -Dwerror=true; meson compile -C "$build_dir"; meson test -C "$build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
full_tests=passed
failure_stage=sanitizer-build-and-test
probe=/var/tmp/glasswyrm-m4-sanitizer-probe
if printf 'int main(void){return 0;}\n' | cc -x c - -fsanitize=address,undefined -o "$probe" >>"$meson_log" 2>&1 && "$probe"; then
  { meson setup "$sanitizer_build_dir" "$source_dir" -Dwerror=true -Dasan=true -Dubsan=true; meson compile -C "$sanitizer_build_dir"; meson test -C "$sanitizer_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
  sanitizer=passed
else sanitizer=unavailable; fi
rm -f "$probe"
failure_stage=compositor-only-build-and-test
{ meson setup "$gwcomp_build_dir" "$source_dir" -Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=true -Dtools=false -Dheadless_backend=true -Drender_software=true; meson compile -C "$gwcomp_build_dir"; meson test -C "$gwcomp_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
compositor_only=passed
failure_stage=ipc-only-regression
{ meson setup "$ipc_build_dir" "$source_dir" --prefix=/usr -Dwerror=true -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false; meson compile -C "$ipc_build_dir"; meson test -C "$ipc_build_dir" --print-errorlogs; } 2>&1 | tee -a "$meson_log"
ipc_only=passed
DESTDIR="$install_root" meson install -C "$ipc_build_dir" >>"$meson_log" 2>&1
staged_lib_dir="$install_root/usr/lib64"; [[ -d "$staged_lib_dir" ]] || staged_lib_dir="$install_root/usr/lib"
export PKG_CONFIG_PATH="$staged_lib_dir/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$install_root" LD_LIBRARY_PATH="$staged_lib_dir"
cc "$source_dir/tests/install/gwipc_c_consumer.c" -o /var/tmp/gwipc-m4-c-consumer $(pkg-config --cflags --libs gwipc)
c++ -std=c++20 "$source_dir/tests/install/gwipc_cpp_consumer.cpp" -o /var/tmp/gwipc-m4-cpp-consumer $(pkg-config --cflags --libs gwipc)
/var/tmp/gwipc-m4-c-consumer; /var/tmp/gwipc-m4-cpp-consumer
typed_consumers=passed

gwcomp="$gwcomp_build_dir/src/gwcomp"
producer="$build_dir/src/gwcomp_m4_producer"
malformed="$build_dir/tests/gwipc_probe_client"
test -x "$gwcomp"; test -x "$producer"; test -x "$malformed"
failure_stage=systemd-runtime
[[ ! -e "$socket_path" ]]
systemd-run --unit="$unit" --property=Type=exec --property=NoNewPrivileges=yes \
  --property=PrivateDevices=yes --property=PrivateTmp=yes \
  --property="BindReadOnlyPaths=$gwcomp_build_dir" --property="BindPaths=$frame_dir" \
  --property=RestrictAddressFamilies=AF_UNIX --property=CapabilityBoundingSet= \
  --property=AmbientCapabilities= --property=Restart=no \
  --property=RuntimeDirectory=glasswyrm-m4 --property=RuntimeDirectoryMode=0700 \
  "$gwcomp" --ipc-socket "$socket_path" --dump-dir "$frame_dir"
unit_invocation_id="$(systemctl show "$unit" -p InvocationID --value)"
[[ "$unit_invocation_id" =~ ^[0-9a-f]{32}$ ]]
for _ in {1..100}; do [[ -S "$socket_path" ]] && break; sleep .1; done
[[ -S "$socket_path" ]]; [[ "$(stat -c %a "$socket_path")" == 600 ]]; [[ "$(stat -c %u "$socket_path")" == "$(id -u)" ]]
run_producer() { "$producer" --socket "$socket_path" --scenario "$1" 2>&1 | tee -a "$2"; }
run_producer basic "$producer_log"; basic_frame=passed
run_producer damage-update "$producer_log"; damage_frame=passed
for scenario in stacking visibility clipping opacity; do run_producer "$scenario" "$producer_log"; printf -v "$scenario" %s passed; done
run_producer buffer-replace "$release_log"; grep -F buffer-released "$release_log"; buffer_release=passed
run_producer detach-remove "$release_log"; grep -F 'reason=2' "$release_log"; detach_remove=passed
run_producer unknown-reference "$producer_log"; unknown_reference=passed
before="$(sha256sum "$frame_dir/frames.jsonl")"
run_producer invalid-metadata "$malformed_log"; systemctl is-active --quiet "$unit"; [[ "$(sha256sum "$frame_dir/frames.jsonl")" == "$before" ]]; invalid_metadata_isolation=passed
run_producer invalid-buffer "$malformed_log"; systemctl is-active --quiet "$unit"; [[ "$(sha256sum "$frame_dir/frames.jsonl")" == "$before" ]]; invalid_buffer_isolation=passed
"$malformed" --socket "$socket_path" --mode malformed-envelope 2>&1 | tee -a "$malformed_log"; systemctl is-active --quiet "$unit"; malformed_peer_isolation=passed
run_producer snapshot-reconnect "$producer_log"; reconnect_snapshot=passed
failure_stage=golden-validation
(cd "$frame_dir" && sha256sum -c "$source_dir/tests/fixtures/m4/SHA256SUMS") 2>&1 | tee -a "$golden_log"
golden_hash_count="$(wc -l <"$source_dir/tests/fixtures/m4/SHA256SUMS")"
(cd "$frame_dir" && sha256sum -- *.ppm frames.jsonl >SHA256SUMS && tar -cf "$artifact_dir/milestone4-frames.tar" -- *.ppm frames.jsonl SHA256SUMS)
tar -tf "$artifact_dir/milestone4-frames.tar" | grep -Fx frames.jsonl; frame_archive=passed
systemctl stop "$unit"; systemctl show "$unit" -p Result --value | grep -Fx success
systemd_runtime=passed; [[ ! -e "$socket_path" ]]; socket_cleanup=passed
failure_stage=
GUEST_SCRIPT
}

collect_milestone4_artifacts() {
  local name path failed=0
  init_artifacts
  for name in "${M4_ARTIFACT_NAMES[@]}"; do
    path="$ARTIFACTS_PATH_ABS/$name"
    guest_run_script 'set -euo pipefail; cat "$1"' "$M4_GUEST_ARTIFACT_DIR/$name" >"$path" 2>&1 || { echo "Unable to collect guest artifact: $name" >>"$path"; failed=1; }
  done
  ssh_arguments
  scp "${SSH_ARGS[@]}" "$SSH_TARGET:$M4_GUEST_ARTIFACT_DIR/milestone4-frames.tar" "$ARTIFACTS_PATH_ABS/milestone4-frames.tar" || failed=1
  if [[ -f "$ARTIFACTS_PATH_ABS/milestone4-frames.tar" ]]; then tar -tf "$ARTIFACTS_PATH_ABS/milestone4-frames.tar" >/dev/null || failed=1; fi
  return "$failed"
}

write_milestone4_summary() {
  local passed=$1 failure=${2:-} facts="$ARTIFACTS_PATH_ABS/milestone4-facts.env" summary="$ARTIFACTS_PATH_ABS/milestone4-summary.json"
  python3 - "$facts" "$summary" "$passed" "$failure" "$M4_REQUIRED_BASE_COMMIT" "${M4_TESTED_COMMIT:-unknown}" <<'PY'
import json, pathlib, sys
facts_path, out, requested, failure, base, tested = sys.argv[1:]
facts = {}
p = pathlib.Path(facts_path)
if p.is_file():
    for line in p.read_text(errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep and key.replace("_", "").isalnum(): facts[key] = value
required = {key: "passed" for key in ("full_tests compositor_only ipc_only typed_consumers basic_frame damage_frame stacking visibility clipping opacity buffer_release invalid_metadata_isolation invalid_buffer_isolation malformed_peer_isolation reconnect_snapshot frame_archive systemd_runtime socket_cleanup".split())}
required.update(scenario_exit="0", x_servers_absent="true", api_version="0.2.0", wire_version="1.0")
errors = [f"{k} must be {v}" for k,v in required.items() if facts.get(k) != v]
if facts.get("sanitizer") not in {"passed", "unavailable"}: errors.append("sanitizer must be passed or unavailable")
try:
    if int(facts.get("golden_hash_count", "0")) < 1: errors.append("golden_hash_count must be positive")
except ValueError: errors.append("golden_hash_count must be numeric")
journal = p.with_name("milestone4-journal.log")
if not journal.is_file() or not journal.stat().st_size: errors.append("current invocation journal is missing or empty")
payload={"required_base_commit":base,"tested_commit":tested,"api_version":facts.get("api_version","unknown"),"wire_version":facts.get("wire_version","unknown"),"xorg_xwayland_absent":facts.get("x_servers_absent")=="true","results":{k:facts.get(k,"unknown") for k in required if k not in {"scenario_exit","x_servers_absent","api_version","wire_version"}},"sanitizer":facts.get("sanitizer","unknown"),"golden_hash_count":facts.get("golden_hash_count","0"),"journal":"passed" if journal.is_file() and journal.stat().st_size else "failed","passed":requested=="true" and not errors,"failure_stage":failure or facts.get("failure_stage",""),"evidence_errors":errors}
pathlib.Path(out).write_text(json.dumps(payload,indent=2)+"\n")
if requested=="true" and errors: raise SystemExit(2)
PY
}

verify_milestone4_source_identity() {
  local status unexpected='' line
  status="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" || return
  while IFS= read -r line; do [[ -z "$line" || "$line" == '?? Plans/'* ]] || unexpected+="${unexpected:+$'\n'}$line"; done <<<"$status"
  [[ -z "$unexpected" ]] || { printf 'Milestone 4 VM acceptance requires committed source outside Plans/.\n%s\n' "$unexpected" >&2; return 1; }
  local current; current="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  [[ -z "$M4_TESTED_COMMIT" || "$current" == "$M4_TESTED_COMMIT" ]] || { echo 'Milestone 4 source commit changed during acceptance.' >&2; return 1; }
}

prepare_milestone4_evidence() {
  M4_TESTED_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)" || return
  verify_milestone4_source_identity || return
  git -C "$REPO_ROOT" merge-base --is-ancestor "$M4_REQUIRED_BASE_COMMIT" "$M4_TESTED_COMMIT" || { printf 'HEAD is not based on required Milestone 4 commit %s\n' "$M4_REQUIRED_BASE_COMMIT" >&2; return 1; }
  rm -f "$ARTIFACTS_PATH_ABS"/milestone4-*
}

clear_milestone4_guest_artifacts() {
  guest_run_script 'set -euo pipefail; artifact_dir=$1; [[ "$artifact_dir" == /var/tmp/glasswyrm-m4-artifacts ]]; rm -rf -- "$artifact_dir"; mkdir -p -- "$artifact_dir"' "$M4_GUEST_ARTIFACT_DIR"
}

milestone4_runtime_test() {
  local approved=$1 failure='' status=0 collection_status=0 script current artifacts_prepared=false
  require_approval milestone4-runtime-test "$approved"; require_vm_domain; init_artifacts; SCENARIO_RECORDS=()
  if prepare_milestone4_evidence; then :; else status=$?; failure=source-evidence; fi
  if [[ -z "$failure" ]]; then if vm_boot; then :; else status=$?; failure=boot; fi; fi
  if [[ -z "$failure" ]]; then if clear_milestone4_guest_artifacts; then artifacts_prepared=true; else status=$?; failure=artifact-preparation; fi; fi
  if [[ -z "$failure" ]]; then if verify_milestone4_source_identity && push_source && verify_milestone4_source_identity; then :; else status=$?; failure=push-source; fi; fi
  if [[ -z "$failure" ]]; then script="$(milestone4_guest_script)"; if capture_guest_action milestone4-runtime-test "$ARTIFACTS_PATH_ABS/milestone4-runtime-test.log" "$script" "$GUEST_SOURCE_PATH" "$M4_GUEST_ARTIFACT_DIR"; then :; else status=$?; failure=guest-runtime; fi; fi
  if [[ "$artifacts_prepared" == true ]]; then collect_milestone4_artifacts || collection_status=$?; fi
  if ((collection_status != 0)) && [[ -z "$failure" ]]; then status=$collection_status; failure=artifact-collection; fi
  if [[ -n "$failure" ]]; then write_milestone4_summary false "$failure" || true; printf 'Milestone 4 VM runtime test failed during: %s\n' "$failure" >&2; print_artifacts >&2; return "${status:-1}"; fi
  current="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  if [[ "$current" != "$M4_TESTED_COMMIT" ]] || ! verify_milestone4_source_identity; then write_milestone4_summary false source-identity-changed; return 1; fi
  write_milestone4_summary true ''; echo 'Milestone 4 VM runtime test passed.'; print_artifacts
}
