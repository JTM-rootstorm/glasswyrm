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
from config_doctor import _modetest_has_selected_mode, doctor, parse_config
from evidence import (
    _copy_fixture_artifacts, _create_archive, analyze_cadence, finalize_live,
    validate_archive, validate_restore,
)
from live_runner import BUILD_ROOT, FIXED_BINARIES, LIVE_UNITS, RUNTIME_ROOT, FixedLiveRunner

def dry_run(config_path: Path, fixture_dir: Path, artifact_dir: Path) -> int:
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
        if doctor(config_path, fixture_dir, artifact_dir) != 0:
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


def milestone14(config_path: Path, confirmed: bool, dry: bool,
                fixture_dir: Path | None, artifact_dir: Path) -> int:
    if not confirmed:
        print("gw-hw: milestone14-vrr-test requires the literal --yes", file=sys.stderr)
        return 2
    if dry:
        if fixture_dir is None:
            print("gw-hw: --dry-run requires --fixture-dir", file=sys.stderr)
            return 2
        return dry_run(config_path, fixture_dir, artifact_dir)
    try:
        _prepare_private_empty_directory(artifact_dir, "live artifact directory")
        _prepare_private_empty_directory(RUNTIME_ROOT, "live runtime directory")
        config = parse_config(config_path)
        if doctor(config_path, None, artifact_dir) != 0:
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
                _write_json(artifact_dir / "milestone14-hardware-summary.json",
                            {"schema": ARTIFACT_SCHEMA, "dry_run": False,
                             "passed": False,
                             "failure_stage": "live-run-or-evidence",
                             "evidence_errors": [str(error)]})
        except OSError:
            pass
        print(f"gw-hw: live run failed: {error}", file=sys.stderr)
        return 1


def self_test() -> int:
    valid = '''drm_device = "/dev/dri/card0"\nconnector = "DP-1"\nmode = "2560x1440@144000"\ntty = "/dev/tty2"\nkeyboard_device = "/dev/input/event0"\npointer_device = "/dev/input/event1"\nexpected_min_refresh_hz = 48\nexpected_max_refresh_hz = 144\ntarget_refresh_hz = 70\nmonitor_model = "reviewed model"\nedid_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\ndebugfs_connector_path = "/sys/kernel/debug/dri/0/DP-1"\n'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "config.toml"
        path.write_text(valid, encoding="utf-8")
        config = parse_config(path)
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
        runner = FixedLiveRunner(config, Path(directory), fake, "/dev/tty2", False,
                                 lambda path, kind: True, lambda: dict(state), False)
        runner.run()
        if runner.steps != list(RUN_STEPS) or not runner.cleanup_attempted:
            raise AssertionError("fixed live runner order or cleanup changed")
        replay_names = {item["name"] for item in runner.snapshot_expectations}
        if not {"milestone14-restart-gwm.json", "milestone14-restart.log"}.issubset(replay_names):
            raise AssertionError("distinct GWM/compositor replay snapshots are absent")
        flattened = [item for argv in commands for item in argv]
        if str(BUILD_ROOT / "src/glasswyrm-session") in flattened:
            raise AssertionError("live runner unexpectedly used the session wrapper")
        server = next((argv for argv in commands
                       if str(FIXED_BINARIES["server"]) in argv), None)
        if (server is None or server.count("--libinput-device") != 2 or
                "/dev/input/event0" not in server or "/dev/input/event1" not in server):
            raise AssertionError("direct server argv omitted fixed libinput devices")
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
        chvt = [argv for argv in commands if argv and argv[0] == str(FIXED_BINARIES["chvt"])]
        if [argv[1] for argv in chvt[:2]] != ["1", "2"]:
            raise AssertionError("VT inactive observation does not precede reacquire")
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
        try:
            finalize_live(config, Path(directory), runner)
        except HarnessError:
            pass
        else:
            raise AssertionError("missing live evidence was accepted")
        from selftest_fixtures import populate_live_evidence
        live = Path(directory) / "positive-live"
        populate_live_evidence(live, config)
        evidence_runner = FixedLiveRunner(
            config, live, fake, "/dev/tty2", False,
            lambda path, kind: True, lambda: dict(state), False)
        evidence_runner.steps = list(RUN_STEPS)
        evidence_runner.before_state = dict(state)
        evidence_runner.after_state = dict(state)
        evidence_runner.cleanup_attempted = True
        finalize_live(config, live, evidence_runner)
        positive = _read_json(live / "milestone14-hardware-summary.json")
        if (positive.get("passed") is not True or
                not (live / "milestone14-vrr-hardware-evidence.tar").is_file() or
                not validate_archive(
                    live, live / "milestone14-vrr-hardware-evidence.tar")["passed"]):
            raise AssertionError("complete live evidence did not pass finalization")
        wrong_tty = FixedLiveRunner(config, Path(directory), fake, "/dev/tty3", False,
                                    lambda path, kind: True, lambda: dict(state), False)
        try:
            wrong_tty.run()
        except HarnessError:
            pass
        else:
            raise AssertionError("wrong live VT was accepted")
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
    print("gw-hw self-test: ok")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="gw-hw", description="Fixed local Glasswyrm hardware harness")
    subparsers = result.add_subparsers(dest="command", required=True)
    doctor_parser = subparsers.add_parser("doctor")
    doctor_parser.add_argument("--config", required=True, type=Path)
    doctor_parser.add_argument("--fixture-dir", type=Path, help=argparse.SUPPRESS)
    doctor_parser.add_argument("--artifact-dir", type=Path)
    milestone_parser = subparsers.add_parser("milestone14-vrr-test")
    milestone_parser.add_argument("--config", required=True, type=Path)
    milestone_parser.add_argument("--yes", action="store_true")
    milestone_parser.add_argument("--dry-run", action="store_true", help=argparse.SUPPRESS)
    milestone_parser.add_argument("--fixture-dir", type=Path, help=argparse.SUPPRESS)
    milestone_parser.add_argument("--artifact-dir", required=True, type=Path)
    subparsers.add_parser("self-test", help=argparse.SUPPRESS)
    return result


def main(arguments: list[str] | None = None) -> int:
    options = parser().parse_args(arguments)
    if options.command == "doctor":
        return doctor(options.config, options.fixture_dir, options.artifact_dir)
    if options.command == "milestone14-vrr-test":
        return milestone14(options.config, options.yes, options.dry_run,
                           options.fixture_dir, options.artifact_dir)
    return self_test()


if __name__ == "__main__":
    raise SystemExit(main())
