"""Guarded orchestration for the minimal M14 NVIDIA VRR truth probe."""

from __future__ import annotations

import os
from pathlib import Path
import stat
import sys

from common import (
    ARTIFACT_SCHEMA, HarnessError, MAX_JSON_BYTES,
    _prepare_private_empty_directory, _read_regular, _write_json,
    require_hardware_test_opt_in,
)
from config_doctor import doctor_config, parse_config
from live_runner import FIXED_BINARIES, FixedLiveRunner, require_live_harness_scope
from nvidia_probe_analysis import analyze_probe, write_summary_exclusive
from provenance import (
    remove_staged_executables, stage_probe_build_provenance,
)


RAW_ARTIFACT = "milestone14-nvidia-vrr-probe.jsonl"
SUMMARY_ARTIFACT = "milestone14-nvidia-vrr-probe-summary.json"
RUN_ARTIFACT = "milestone14-nvidia-vrr-probe-run.json"
FIXTURE_ARTIFACT = "nvidia-vrr-probe.jsonl"
PROBE_RUNTIME_ROOT = Path("/run/glasswyrm-m14-nvidia-probe")
PROBE_VERIFIED_BIN_ROOT = PROBE_RUNTIME_ROOT / "verified-bin"


def _copy_regular_exclusive(source: Path, destination: Path) -> None:
    contents = _read_regular(source, MAX_JSON_BYTES)
    descriptor = os.open(
        destination,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o600,
    )
    try:
        offset = 0
        while offset < len(contents):
            offset += os.write(descriptor, contents[offset:])
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _safe_artifact_directory(path: Path) -> bool:
    try:
        status = path.lstat()
    except OSError:
        return False
    return stat.S_ISDIR(status.st_mode) and not status.st_mode & 0o077


def run_nvidia_vrr_probe(
        config_path: Path, artifact_dir: Path, confirmed: bool,
        fixture_dir: Path | None = None) -> int:
    """Run or replay the bounded probe without starting the Glasswyrm stack."""
    if not confirmed:
        print(
            "gw-hw: milestone14-nvidia-vrr-probe requires the literal --yes",
            file=sys.stderr,
        )
        return 2

    if fixture_dir is None:
        try:
            require_hardware_test_opt_in()
        except HarnessError as error:
            print(f"gw-hw: NVIDIA VRR probe failed during guard: {error}",
                  file=sys.stderr)
            return 1

    stage = "scope"
    command_evidence: dict[str, object] | None = None
    try:
        if fixture_dir is None:
            require_live_harness_scope()
        stage = "artifacts"
        _prepare_private_empty_directory(
            artifact_dir, "NVIDIA VRR probe artifact directory")
        stage = "configuration"
        config = parse_config(config_path)
        stage = "doctor"
        if doctor_config(
                config, fixture_dir, artifact_dir, require_input=False,
                validate_provenance=False) != 0:
            raise HarnessError("NVIDIA VRR probe doctor failed")
        if fixture_dir is None:
            _prepare_private_empty_directory(
                PROBE_RUNTIME_ROOT, "NVIDIA VRR probe runtime directory")
            try:
                staged_binaries = stage_probe_build_provenance(
                    str(config["tested_commit"]), PROBE_VERIFIED_BIN_ROOT,
                    artifact_dir)
            except Exception:
                PROBE_RUNTIME_ROOT.rmdir()
                raise

        report = artifact_dir / RAW_ARTIFACT
        stage = "probe"
        if fixture_dir is not None:
            _copy_regular_exclusive(fixture_dir / FIXTURE_ARTIFACT, report)
            command_evidence = {
                "fixture": True,
                "exit_status": 0,
                "fixed_executable": "gw_drm_vrr_probe",
            }
        else:
            fixed_binaries = {**FIXED_BINARIES, **staged_binaries}
            try:
                runner = FixedLiveRunner(
                    config, artifact_dir, detached_invocation=True,
                    fixed_binaries=fixed_binaries)
                argv = [
                    str(fixed_binaries["nvidia-drm-vrr-probe"]),
                    "--device", str(config["drm_device"]),
                    "--connector", str(config["connector"]),
                    "--mode", str(config["mode"]),
                    "--run-id", str(config["tested_commit"])[:32],
                    "--output", str(report),
                    "--target-hz", str(config["target_refresh_hz"]),
                    "--warmup", "10",
                    "--samples", "140",
                ]
                result = runner.command_result(argv)
                command_evidence = runner._command_evidence(result)
                if not result.succeeded:
                    runner._write_command_diagnostic(argv, result)
                    raise HarnessError("fixed NVIDIA VRR probe command failed")
            finally:
                remove_staged_executables(
                    PROBE_VERIFIED_BIN_ROOT, staged_binaries)
                PROBE_RUNTIME_ROOT.rmdir()

        stage = "analysis"
        summary = analyze_probe(report, config)
        write_summary_exclusive(artifact_dir / SUMMARY_ARTIFACT, summary)
        run_summary = {
            "schema": ARTIFACT_SCHEMA,
            "stage": "nvidia-probe",
            "passed": summary["passed"],
            "failure_stage": None if summary["passed"] else "analysis",
            "classification": summary["classification"],
            "restoration_passed": summary["restoration_passed"],
            "input_devices_required": False,
            "command": command_evidence,
        }
        _write_json(artifact_dir / RUN_ARTIFACT, run_summary)
        print(
            "gw-hw: NVIDIA VRR probe classification: "
            f"{summary['classification']}"
        )
        return 0 if summary["passed"] else 1
    except (HarnessError, OSError, ValueError) as error:
        if _safe_artifact_directory(artifact_dir):
            _write_json(artifact_dir / RUN_ARTIFACT, {
                "schema": ARTIFACT_SCHEMA,
                "stage": "nvidia-probe",
                "passed": False,
                "failure_stage": stage,
                "error": str(error),
                "input_devices_required": False,
                "command": command_evidence,
            })
        print(f"gw-hw: NVIDIA VRR probe failed during {stage}: {error}",
              file=sys.stderr)
        return 1
