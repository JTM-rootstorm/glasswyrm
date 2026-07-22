"""CLI routing and injected self-tests for the fixed M14 hardware harness."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

from common import (
    ARCHIVE_STATE_ARTIFACTS, ARTIFACT_SCHEMA, ConfigError, HarnessError,
    RUN_STEPS, _prepare_private_empty_directory, _read_json, _write_json,
)
from config_doctor import (
    _modetest_connector_property_value, _modetest_crtc_property_value,
    _modetest_has_selected_mode, _modetest_selected_crtc_id, doctor,
    parse_config, validate_cli_identity,
)
from evidence import (
    _copy_fixture_artifacts, _create_archive, analyze_cadence, finalize_live,
    validate_archive, validate_restore,
)
from live_runner import (
    BUILD_ROOT, FIXED_BINARIES, LIVE_UNITS, RUNTIME_ROOT, FixedLiveRunner,
    _control_group_has_live_scope, require_live_harness_scope,
)

def dry_run(config_path: Path, required_base: str, tested_commit: str,
            fixture_dir: Path, artifact_dir: Path) -> int:
    if artifact_dir.exists():
        status = artifact_dir.lstat()
        if not stat.S_ISDIR(status.st_mode) or status.st_mode & 0o077 or any(artifact_dir.iterdir()):
            print("gw-hw: artifact directory must be a private empty non-symlink directory", file=sys.stderr)
            return 1
    else:
        artifact_dir.mkdir(mode=0o700, parents=True)
    stage = "doctor"
    errors: list[str] = []
    try:
        config = parse_config(config_path)
        validate_cli_identity(config, required_base, tested_commit)
        if doctor(config_path, required_base, tested_commit,
                  fixture_dir, artifact_dir) != 0:
            raise HarnessError("doctor failed")
        stage = "artifact collection"
        _copy_fixture_artifacts(fixture_dir, artifact_dir)
        stage = "cadence analysis"
        report = artifact_dir / "milestone14-vrr-report.jsonl"
        off = analyze_cadence(report, config, False)
        on = analyze_cadence(report, config, True)
        _write_json(artifact_dir / "milestone14-vrr-off-summary.json", off)
        _write_json(artifact_dir / "milestone14-vrr-on-summary.json", on)
        if not off["passed"] or not on["passed"]:
            raise HarnessError("cadence acceptance failed")
        stage = "restoration"
        restore = validate_restore(fixture_dir / "state-before.json", fixture_dir / "state-after.json")
        shutil.copyfile(fixture_dir / "state-before.json", artifact_dir / ARCHIVE_STATE_ARTIFACTS[0])
        shutil.copyfile(fixture_dir / "state-after.json", artifact_dir / ARCHIVE_STATE_ARTIFACTS[1])
        _write_json(artifact_dir / "milestone14-restore.json", restore)
        if not restore["passed"]:
            raise HarnessError("exact restoration readback failed")
        stage = "summary"
        summary = {"schema": ARTIFACT_SCHEMA, "dry_run": True, "passed": True,
                   "required_base_commit": config["required_base_commit"],
                   "tested_commit": config["tested_commit"],
                   "run_step_count": len(RUN_STEPS), "failure_stage": None,
                   "evidence_errors": [], "enabled_pass_percentage": on["pass_percentage"],
                   "disabled_pass_percentage": off["pass_percentage"], "restoration": True,
                   "archive_validation": True}
        _write_json(artifact_dir / "milestone14-hardware-summary.json", summary)
        stage = "archive"
        archive = _create_archive(artifact_dir)
        validation = validate_archive(artifact_dir, archive)
        if not validation["passed"]:
            raise HarnessError("; ".join(validation["errors"]))
        print("gw-hw: deterministic Milestone 14 dry-run passed")
        return 0
    except (HarnessError, OSError) as error:
        errors.append(str(error))
        summary = {"schema": ARTIFACT_SCHEMA, "dry_run": True, "passed": False,
                   "run_step_count": len(RUN_STEPS), "failure_stage": stage,
                   "evidence_errors": errors, "restoration_attempted": stage != "doctor"}
        _write_json(artifact_dir / "milestone14-hardware-summary.json", summary)
        print(f"gw-hw: dry-run failed during {stage}: {error}", file=sys.stderr)
        return 1


def _live_failure_summary(
        error: Exception, runner: FixedLiveRunner | None,
        artifact_dir: Path) -> dict[str, object]:
    primary = str(error)
    restoration_errors: list[str] = []
    restoration: bool | None = None
    restoration_evidence: dict[str, object] | None = None
    restoration_attempted = runner is not None and runner.cleanup_attempted
    if runner is not None:
        restoration_errors.extend(runner.cleanup_errors)
    restore_path = artifact_dir / "milestone14-restore.json"
    if restore_path.is_file():
        try:
            restore = _read_json(restore_path)
            if isinstance(restore.get("passed"), bool):
                restoration = restore["passed"]
            restoration_evidence = {
                name: restore.get(name) for name in
                ("original_console", "restored_console", "checks", "errors")
                if name in restore
            }
            checks = restore.get("checks")
            if isinstance(checks, dict):
                restoration_errors.extend(
                    f"restoration check failed: {name}"
                    for name, passed in sorted(checks.items()) if passed is not True)
            exact_errors = restore.get("errors")
            if isinstance(exact_errors, list):
                restoration_errors.extend(
                    value for value in exact_errors if isinstance(value, str))
            if restore.get("passed") is not True and not restoration_errors:
                restoration_errors.append("restoration evidence did not pass")
        except (HarnessError, OSError, json.JSONDecodeError) as restore_error:
            restoration_errors.append(
                f"restoration evidence unreadable: {restore_error}")
    restoration_errors = list(dict.fromkeys(restoration_errors))
    evidence_errors = [primary]
    evidence_errors.extend(
        value for value in restoration_errors if value not in evidence_errors)
    return {
        "schema": ARTIFACT_SCHEMA,
        "dry_run": False,
        "passed": False,
        "failure_stage": "live-run-or-evidence",
        "primary_error": primary,
        "evidence_errors": evidence_errors,
        "restoration_attempted": restoration_attempted,
        "restoration": restoration,
        "restoration_evidence": restoration_evidence,
        "restoration_errors": restoration_errors,
    }


def milestone14(config_path: Path, required_base: str, tested_commit: str,
                confirmed: bool, dry: bool,
                fixture_dir: Path | None, artifact_dir: Path) -> int:
    if not confirmed:
        print("gw-hw: milestone14-vrr-test requires the literal --yes", file=sys.stderr)
        return 2
    if dry:
        if fixture_dir is None:
            print("gw-hw: --dry-run requires --fixture-dir", file=sys.stderr)
            return 2
        return dry_run(config_path, required_base, tested_commit,
                       fixture_dir, artifact_dir)
    runner: FixedLiveRunner | None = None
    try:
        require_live_harness_scope()
        _prepare_private_empty_directory(artifact_dir, "live artifact directory")
        _prepare_private_empty_directory(RUNTIME_ROOT, "live runtime directory")
        config = parse_config(config_path)
        validate_cli_identity(config, required_base, tested_commit)
        if doctor(config_path, required_base, tested_commit,
                  None, artifact_dir) != 0:
            raise HarnessError("live doctor failed")
        runner = FixedLiveRunner(config, artifact_dir)
        runner.run()
        finalize_live(config, artifact_dir, runner)
        return 0
    except (HarnessError, OSError, json.JSONDecodeError,
            subprocess.SubprocessError) as error:
        try:
            status = artifact_dir.lstat()
            if stat.S_ISDIR(status.st_mode) and not status.st_mode & 0o077:
                _write_json(
                    artifact_dir / "milestone14-hardware-summary.json",
                    _live_failure_summary(error, runner, artifact_dir))
        except OSError:
            pass
        print(f"gw-hw: live run failed: {error}", file=sys.stderr)
        return 1


def self_test() -> int:
    valid = '''required_base_commit = "6864ea631d61636289a21c7d2d6655a17be0c004"\ntested_commit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"\ndrm_device = "/dev/dri/card0"\nconnector = "DP-1"\nmode = "2560x1440@144000"\ntty = "/dev/tty2"\nalternate_tty = "/dev/tty1"\nkeyboard_device = "/dev/input/event0"\npointer_device = "/dev/input/event1"\nexpected_min_refresh_hz = 48\nexpected_max_refresh_hz = 144\ntarget_refresh_hz = 70\nmonitor_model = "reviewed model"\nedid_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\ndebugfs_connector_path = "/sys/kernel/debug/dri/0/DP-1"\n'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "config.toml"
        path.write_text(valid, encoding="utf-8")
        config = parse_config(path)
        validate_cli_identity(
            config, "6864ea631d61636289a21c7d2d6655a17be0c004",
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
        cadence_report = Path(directory) / "scenario-bounded-cadence.jsonl"
        cadence_lines: list[str] = []
        for index in range(131):
            cadence_lines.append(json.dumps({
                "record": "vrr-timing", "sequence": index,
                "kernel_timestamp_nanoseconds": 1_000_000_000 +
                    14_285_714 * index,
                "effective_enabled": True,
            }, sort_keys=True) + "\n")
        cadence_end = len("".join(cadence_lines).encode("utf-8"))
        for index in range(512):
            cadence_lines.append(json.dumps({
                "record": "vrr-timing", "sequence": 1_000 + index,
                "kernel_timestamp_nanoseconds": 10_000_000_000 +
                    6_944_444 * index,
                "effective_enabled": True,
            }, sort_keys=True) + "\n")
        cadence_report.write_text("".join(cadence_lines), encoding="utf-8")
        bounded = analyze_cadence(
            cadence_report, config, True, (0, cadence_end), "on-cadence")
        contaminated = analyze_cadence(cadence_report, config, True)
        if (bounded.get("passed") is not True or
                bounded.get("sample_count") != 130 or
                bounded.get("scenario") != "on-cadence" or
                contaminated.get("passed") is not False):
            raise AssertionError("cadence analysis crossed its scenario boundary")
        for required_base, tested_commit in (
                ("0" * 40, "b" * 40),
                ("6864ea631d61636289a21c7d2d6655a17be0c004", "c" * 40)):
            try:
                validate_cli_identity(config, required_base, tested_commit)
            except ConfigError:
                pass
            else:
                raise AssertionError("mismatched CLI identity was accepted")
        modetest_fixture = '''Connectors:
id encoder status name size (mm) modes encoders
42 41 connected DP-2 600x340 1 41
  modes:
    #0 2560x1440 144.00 2560 2608 2640 2720 1440 1443 1448 1471
  props:
43 0 disconnected DP-1 0x0 0 0
  modes:
44 41 connected DP-1 600x340 1 41
  modes:
    #0 2560x1440 144.00 2560 2608 2640 2720 1440 1443 1448 1471
  props:
'''
        if not _modetest_has_selected_mode(
                modetest_fixture, "DP-1", "2560x1440@144000"):
            raise AssertionError("exact selected connector mode was not detected")
        if (_modetest_has_selected_mode(
                modetest_fixture, "DP-1", "2560x1440@143970") or
                _modetest_has_selected_mode(
                    modetest_fixture, "DP-3", "2560x1440@144000")):
            raise AssertionError("wrong refresh or connector mode was accepted")
        property_fixture = '''Connectors:
42 41 connected DP-2 600x340 1 41
  props:
    9 vrr_capable:
      flags: immutable range
      values: 0 1
      value: 1
43 41 connected DP-1 600x340 1 41
  props:
    9 vrr_capable:
      flags: immutable range
      values: 0 1
      value: 0
'''
        if (_modetest_connector_property_value(
                property_fixture, "DP-1", "vrr_capable") != 0 or
                _modetest_connector_property_value(
                    property_fixture, "DP-2", "vrr_capable") != 1 or
                _modetest_connector_property_value(
                    property_fixture, "DP-3", "vrr_capable") is not None):
            raise AssertionError("connector property parsing crossed connector boundaries")
        crtc_fixture = '''Encoders:
id crtc type possible crtcs possible clones
41 77 TMDS 0x00000001 0x00000000
42 88 TMDS 0x00000002 0x00000000
43 66 TMDS 0x00000004 0x00000000
Connectors:
42 41 connected DP-1 600x340 1 41
43 42 connected DP-2 600x340 1 42
44 43 connected DP-3 600x340 1 43
CRTCs:
id fb pos size
66 89 (0,0) (1920x1080)
  props:
    11 ACTIVE:
      flags: range
      values: 0 1
      value: 1
77 90 (0,0) (2560x1440)
  props:
    12 VRR_ENABLED:
      flags: range
      values: 0 1
      value: 0
88 91 (2560,0) (2560x1440)
  props:
    12 VRR_ENABLED:
      flags: range
      values: 0 1
      value: 1
99 0 (0,0) (0x0)
  props:
    12 VRR_ENABLED:
      flags: range
      values: 0 1
      value: 1
'''
        dp1_crtc = _modetest_selected_crtc_id(crtc_fixture, "DP-1")
        dp2_crtc = _modetest_selected_crtc_id(crtc_fixture, "DP-2")
        dp3_crtc = _modetest_selected_crtc_id(crtc_fixture, "DP-3")
        if (dp1_crtc != 77 or dp2_crtc != 88 or dp3_crtc != 66 or
                _modetest_selected_crtc_id(crtc_fixture, "DP-4") is not None or
                _modetest_crtc_property_value(
                    crtc_fixture, dp1_crtc, "VRR_ENABLED") != 0 or
                _modetest_crtc_property_value(
                    crtc_fixture, dp2_crtc, "VRR_ENABLED") != 1 or
                _modetest_crtc_property_value(
                    crtc_fixture, dp3_crtc, "VRR_ENABLED") is not None or
                _modetest_crtc_property_value(
                    crtc_fixture, 100, "VRR_ENABLED") is not None):
            raise AssertionError("CRTC property parsing crossed pipeline boundaries")
        for addition in ('command = "rm -rf /"\n', 'password = "secret"\n', 'package = "x11-base/glasswyrm"\n'):
            path.write_text(valid + addition, encoding="utf-8")
            try:
                parse_config(path)
            except ConfigError:
                pass
            else:
                raise AssertionError("forbidden arbitrary field was accepted")
        path.write_text(valid.replace("target_refresh_hz = 70", "target_refresh_hz = 72"), encoding="utf-8")
        try:
            parse_config(path)
        except ConfigError:
            pass
        else:
            raise AssertionError("fixed-refresh divisor was accepted")
        commands: list[list[str]] = []
        def fake(argv: list[str], output: Path | None) -> int:
            commands.append(argv)
            return 1 if argv[1:3] == ["is-active", "display-manager.service"] else 0
        state = {"active_vt": 2, "kd_mode": 0, "getty_active": True}
        console_rechecks = 0
        def console_preflight() -> dict[str, object]:
            nonlocal console_rechecks
            console_rechecks += 1
            return {"active_tty": "/dev/tty2",
                    "kd_modes": {"/dev/tty2": 0, "/dev/tty1": 0}}
        runner = FixedLiveRunner(config, Path(directory), fake, "/dev/tty2", False,
                                 lambda path, kind: True, lambda: dict(state), False,
                                 console_preflight)
        runner.run()
        if (runner.steps != list(RUN_STEPS) or not runner.cleanup_attempted or
                runner.cleanup_wait_count != 7 or console_rechecks != 2):
            raise AssertionError("fixed live runner order or cleanup changed")
        transient_units = [argv for argv in commands
                           if argv and argv[0] ==
                           str(FIXED_BINARIES["systemd-run"])]
        if not transient_units:
            raise AssertionError("fixed live runner did not start transient units")
        for argv in transient_units:
            unit = next((value.removeprefix("--unit=") for value in argv
                         if value.startswith("--unit=")), None)
            if unit is None:
                raise AssertionError("transient unit omitted its fixed name")
            expected = str(Path(directory) / f"{unit}.log")
            if (f"--property=StandardOutput=append:{expected}" not in argv or
                    f"--property=StandardError=append:{expected}" not in argv):
                raise AssertionError(
                    "transient unit output is not preserved in its artifact log")
        replay_names = {item["name"] for item in runner.snapshot_expectations}
        if not {"milestone14-restart-gwm.json", "milestone14-restart.log"}.issubset(replay_names):
            raise AssertionError("distinct GWM/compositor replay snapshots are absent")
        app_states = {(item["name"], item["effective"], item["preference"])
                      for item in runner.snapshot_expectations
                      if str(item["name"]).startswith("milestone14-app-requested")}
        if app_states != {
                ("milestone14-app-requested-default.json", False, "default"),
                ("milestone14-app-requested.log", True, "prefer"),
                ("milestone14-app-requested-disable.json", False, "disable")}:
            raise AssertionError("AppRequested authoritative transition proof changed")
        flattened = [item for argv in commands for item in argv]
        if str(BUILD_ROOT / "src/glasswyrm-session") in flattened:
            raise AssertionError("live runner unexpectedly used the session wrapper")
        server = next((argv for argv in commands
                       if str(FIXED_BINARIES["server"]) in argv), None)
        if (server is None or server.count("--game-compat") != 1 or
                server.count("--libinput-device") != 2 or
                "/dev/input/event0" not in server or "/dev/input/event1" not in server):
            raise AssertionError("direct server argv omitted the game profile or fixed libinput devices")
        if not any(argv[1:3] == ["restart", LIVE_UNITS["gwm"]]
                   for argv in commands):
            raise AssertionError("GWM restart is not independently supervised")
        compositor_stops = [argv for argv in commands
                            if argv[1:3] == ["stop", LIVE_UNITS["gwcomp"]]]
        compositor_starts = [argv for argv in commands
                             if argv[1:3] == ["start", LIVE_UNITS["gwcomp"]]]
        if not compositor_stops or not compositor_starts:
            raise AssertionError("compositor restart is not independently supervised")
        if not any("preference" in argv and "client-app-preferences.json" in " ".join(argv)
                   for argv in commands):
            raise AssertionError("one-window preference scenario is absent")
        repaint_client = next((argv for argv in commands
                               if "client-always.json" in " ".join(argv)), None)
        if (repaint_client is None or
                repaint_client.count("--repaint-trigger") != 1 or
                repaint_client.count("--repaint-count") != 1 or
                "2" not in repaint_client):
            raise AssertionError("bounded physical repaint trigger is absent")
        snapshot_attempts = 0
        def converging(argv: list[str], output: Path | None) -> int:
            nonlocal snapshot_attempts
            snapshot_attempts += 1
            assert output is not None
            policy = "off" if snapshot_attempts == 1 else "always-eligible"
            _write_json(output, {"vrr": [{"name": "DP-1", "policy": policy,
                                          "effective_enabled": snapshot_attempts > 1,
                                          "hardware_capable": True,
                                          "kms_controllable": True,
                                          "simulated": False}], "windows": []})
            return 0
        polling = FixedLiveRunner(config, Path(directory), converging,
                                  "/dev/tty2", False, validate_runtime=True)
        polling.snapshot("polling.json", "always-eligible", True)
        if snapshot_attempts != 2 or not (Path(directory) / "polling.json").is_file():
            raise AssertionError("snapshot polling did not atomically promote converged state")
        cleanup_attempts = 0
        def cleanup_converging(argv: list[str], output: Path | None) -> int:
            nonlocal cleanup_attempts
            cleanup_attempts += 1
            assert output is not None
            windows = ([{"window": 41}] if cleanup_attempts == 1 else [])
            candidate = 41 if cleanup_attempts == 2 else 0
            _write_json(output, {"vrr": [{"name": "DP-1",
                                            "candidate_window": candidate}],
                                 "windows": windows})
            return 0
        cleanup_polling = FixedLiveRunner(
            config, Path(directory), cleanup_converging, "/dev/tty2", False,
            validate_runtime=True)
        cleanup_polling.wait_policy_cleanup()
        if cleanup_attempts != 3 or cleanup_polling.cleanup_wait_count != 1:
            raise AssertionError("cleanup polling accepted stale windows or candidates")
        chvt = [argv for argv in commands if argv and argv[0] == str(FIXED_BINARIES["chvt"])]
        if [argv[1] for argv in chvt[:2]] != ["1", "2"]:
            raise AssertionError("VT inactive observation does not precede reacquire")
        restoration_root = Path(directory) / "exact-restoration"
        restoration_root.mkdir()
        restored_state = {"active_vt": 1, "kd_mode": 1, "getty_active": False}
        restoration_runner = FixedLiveRunner(
            config, restoration_root, fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(restored_state), False)
        restoration_runner.before_state = dict(state)
        restoration_runner.cleanup()
        restoration = _read_json(
            restoration_root / "milestone14-restore.json")
        if (restoration.get("original_console") != state or
                restoration.get("restored_console") != restored_state or
                restoration.get("checks") != {
                    "active_vt": False, "kd_mode": False,
                    "getty_active": False} or
                not any("active VT restoration mismatch: expected 2, observed 1" == item
                        for item in restoration.get("errors", [])) or
                restoration.get("passed") is not False):
            raise AssertionError("cleanup omitted exact console restoration evidence")
        calls = 0
        def failing(argv: list[str], output: Path | None) -> int:
            nonlocal calls
            calls += 1
            if argv[1:3] == ["is-active", "display-manager.service"]:
                return 1
            return 1 if calls == 8 else 0
        failed_runner = FixedLiveRunner(config, Path(directory), failing, "/dev/tty2", False,
                                        lambda path, kind: True, lambda: dict(state), False)
        try:
            failed_runner.run()
        except HarnessError:
            pass
        else:
            raise AssertionError("fixed live runner failure did not propagate")
        if not failed_runner.cleanup_attempted:
            raise AssertionError("fixed live runner failure skipped cleanup")
        failure_artifacts = Path(directory) / "failure-summary"
        failure_artifacts.mkdir()
        _write_json(failure_artifacts / "milestone14-restore.json", {
            "checks": {"cleanup": False, "kms": True, "vt": False},
            "errors": ["active VT restoration mismatch: expected 2, observed 1"],
            "passed": False,
        })
        failed_runner.cleanup_errors = ["cleanup command failed: systemctl stop"]
        failure_summary = _live_failure_summary(
            HarnessError("primary live failure"), failed_runner,
            failure_artifacts)
        if (failure_summary.get("primary_error") != "primary live failure" or
                failure_summary.get("evidence_errors", [None])[0] !=
                    "primary live failure" or
                failure_summary.get("restoration_attempted") is not True or
                failure_summary.get("restoration") is not False or
                "cleanup command failed: systemctl stop" not in
                    failure_summary.get("restoration_errors", []) or
                "restoration check failed: vt" not in
                    failure_summary.get("restoration_errors", []) or
                "active VT restoration mismatch: expected 2, observed 1" not in
                    failure_summary.get("restoration_errors", [])):
            raise AssertionError("failure summary masked cleanup or restoration evidence")
        try:
            finalize_live(config, Path(directory), runner)
        except HarnessError:
            pass
        else:
            raise AssertionError("missing live evidence was accepted")
        from selftest_fixtures import populate_live_evidence
        live = Path(directory) / "positive-live"
        cadence_ranges = populate_live_evidence(live, config)
        evidence_runner = FixedLiveRunner(
            config, live, fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False)
        evidence_runner.steps = list(RUN_STEPS)
        evidence_runner.before_state = dict(state)
        evidence_runner.after_state = dict(state)
        evidence_runner.cleanup_attempted = True
        evidence_runner.cadence_ranges = cadence_ranges
        finalize_live(config, live, evidence_runner)
        positive = _read_json(live / "milestone14-hardware-summary.json")
        if (positive.get("passed") is not True or
                not (live / "milestone14-vrr-hardware-evidence.tar").is_file() or
                not validate_archive(
                    live, live / "milestone14-vrr-hardware-evidence.tar")["passed"]):
            raise AssertionError("complete live evidence did not pass finalization")
        mismatched = dict(positive, tested_commit="c" * 40)
        _write_json(live / "milestone14-hardware-summary.json", mismatched)
        _create_archive(live)
        if validate_archive(
                live, live / "milestone14-vrr-hardware-evidence.tar")["passed"]:
            raise AssertionError("cross-evidence source identity mismatch was accepted")
        _write_json(live / "milestone14-hardware-summary.json", positive)
        _create_archive(live)
        on_summary_path = live / "milestone14-vrr-on-summary.json"
        on_summary = _read_json(on_summary_path)
        invalid_cadence = dict(on_summary)
        invalid_cadence["source_byte_range"] = dict(
            on_summary["source_byte_range"],
            start=on_summary["source_byte_range"]["start"] + 1)
        _write_json(on_summary_path, invalid_cadence)
        _create_archive(live)
        if validate_archive(
                live, live / "milestone14-vrr-hardware-evidence.tar")["passed"]:
            raise AssertionError("unaligned cadence scenario evidence was accepted")
        _write_json(on_summary_path, on_summary)
        _create_archive(live)
        invalid_app = Path(directory) / "invalid-app-live"
        invalid_ranges = populate_live_evidence(invalid_app, config)
        app_snapshot = _read_json(invalid_app / "milestone14-app-requested.log")
        app_snapshot["vrr"][0]["effective_enabled"] = False
        _write_json(invalid_app / "milestone14-app-requested.log", app_snapshot)
        invalid_runner = FixedLiveRunner(
            config, invalid_app, fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False)
        invalid_runner.steps = list(RUN_STEPS)
        invalid_runner.before_state = dict(state)
        invalid_runner.after_state = dict(state)
        invalid_runner.cleanup_attempted = True
        invalid_runner.cadence_ranges = invalid_ranges
        try:
            finalize_live(config, invalid_app, invalid_runner)
        except HarnessError:
            pass
        else:
            raise AssertionError("disabled AppRequested Prefer snapshot was accepted")
        for label, artifact, section, replacement in (
                ("default-output-reason", "milestone14-app-requested-default.json",
                 "vrr", ["no-candidate", "policy-off"]),
                ("disable-window-reason", "milestone14-app-requested-disable.json",
                 "windows", ["window-preference-disabled", "window-unfocused"])):
            invalid_exact = Path(directory) / f"invalid-{label}"
            invalid_exact_ranges = populate_live_evidence(invalid_exact, config)
            invalid_snapshot = _read_json(invalid_exact / artifact)
            invalid_snapshot[section][0]["reasons"] = replacement
            _write_json(invalid_exact / artifact, invalid_snapshot)
            invalid_exact_runner = FixedLiveRunner(
                config, invalid_exact, fake, "/dev/tty2", False,
                lambda path, kind: True, lambda: dict(state), False)
            invalid_exact_runner.steps = list(RUN_STEPS)
            invalid_exact_runner.before_state = dict(state)
            invalid_exact_runner.after_state = dict(state)
            invalid_exact_runner.cleanup_attempted = True
            invalid_exact_runner.cadence_ranges = invalid_exact_ranges
            try:
                finalize_live(config, invalid_exact, invalid_exact_runner)
            except HarnessError:
                pass
            else:
                raise AssertionError(
                    f"inexact AppRequested reasons were accepted: {label}")
        invalid_mask = Path(directory) / "invalid-app-mask"
        invalid_mask_ranges = populate_live_evidence(invalid_mask, config)
        default_state = _read_json(invalid_mask / "client-app-default.json")
        default_state["reason_mask"] = 0
        _write_json(invalid_mask / "client-app-default.json", default_state)
        invalid_mask_runner = FixedLiveRunner(
            config, invalid_mask, fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False)
        invalid_mask_runner.steps = list(RUN_STEPS)
        invalid_mask_runner.before_state = dict(state)
        invalid_mask_runner.after_state = dict(state)
        invalid_mask_runner.cleanup_attempted = True
        invalid_mask_runner.cadence_ranges = invalid_mask_ranges
        try:
            finalize_live(config, invalid_mask, invalid_mask_runner)
        except HarnessError:
            pass
        else:
            raise AssertionError("zero AppRequested rejection mask was accepted")
        wrong_tty = FixedLiveRunner(config, Path(directory), fake, "/dev/tty3", False,
                                    lambda path, kind: True, lambda: dict(state), False)
        try:
            wrong_tty.run()
        except HarnessError:
            pass
        else:
            raise AssertionError("wrong live VT was accepted")
        changed_active = FixedLiveRunner(
            config, Path(directory), fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False,
            lambda: {"active_tty": "/dev/tty1",
                     "kd_modes": {"/dev/tty2": 0, "/dev/tty1": 0}})
        try:
            changed_active.preflight()
        except HarnessError as error:
            if "active VT changed" not in str(error):
                raise
        else:
            raise AssertionError("just-in-time active VT drift was accepted")
        graphical_alternate = FixedLiveRunner(
            config, Path(directory), fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False,
            lambda: {"active_tty": "/dev/tty2",
                     "kd_modes": {"/dev/tty2": 0, "/dev/tty1": 1}})
        try:
            graphical_alternate.preflight()
        except HarnessError as error:
            if "KD_TEXT on /dev/tty1" not in str(error):
                raise
        else:
            raise AssertionError("just-in-time alternate KD_GRAPHICS was accepted")
        def graphical(argv: list[str], output: Path | None) -> int:
            return 0
        graphical_runner = FixedLiveRunner(config, Path(directory), graphical, "/dev/tty2", False,
                                            lambda path, kind: True, lambda: dict(state), False)
        try:
            graphical_runner.run()
        except HarnessError:
            pass
        else:
            raise AssertionError("active graphical session was accepted")
    if len(RUN_STEPS) != 29:
        raise AssertionError("fixed hardware run order changed")
    if (not _control_group_has_live_scope(
            "0::/system.slice/glasswyrm-m14-harness.scope\n") or
            _control_group_has_live_scope(
                "0::/system.slice/not-glasswyrm-m14-harness.scope\n") or
            _control_group_has_live_scope(
                "0::/system.slice/glasswyrm-m14-harness.service\n")):
        raise AssertionError("live harness scope identity is not exact")
    print("gw-hw self-test: ok")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="gw-hw", description="Fixed local Glasswyrm hardware harness")
    subparsers = result.add_subparsers(dest="command", required=True)
    doctor_parser = subparsers.add_parser("doctor")
    doctor_parser.add_argument("--config", required=True, type=Path)
    doctor_parser.add_argument("--required-base", required=True)
    doctor_parser.add_argument("--tested-commit", required=True)
    doctor_parser.add_argument("--fixture-dir", type=Path, help=argparse.SUPPRESS)
    doctor_parser.add_argument("--artifact-dir", type=Path)
    milestone_parser = subparsers.add_parser("milestone14-vrr-test")
    milestone_parser.add_argument("--config", required=True, type=Path)
    milestone_parser.add_argument("--required-base", required=True)
    milestone_parser.add_argument("--tested-commit", required=True)
    milestone_parser.add_argument("--yes", action="store_true")
    milestone_parser.add_argument("--dry-run", action="store_true", help=argparse.SUPPRESS)
    milestone_parser.add_argument("--fixture-dir", type=Path, help=argparse.SUPPRESS)
    milestone_parser.add_argument("--artifact-dir", required=True, type=Path)
    subparsers.add_parser("self-test", help=argparse.SUPPRESS)
    return result


def main(arguments: list[str] | None = None) -> int:
    options = parser().parse_args(arguments)
    if options.command == "doctor":
        return doctor(options.config, options.required_base,
                      options.tested_commit, options.fixture_dir,
                      options.artifact_dir)
    if options.command == "milestone14-vrr-test":
        return milestone14(options.config, options.required_base,
                           options.tested_commit, options.yes, options.dry_run,
                           options.fixture_dir, options.artifact_dir)
    return self_test()


if __name__ == "__main__":
    raise SystemExit(main())
