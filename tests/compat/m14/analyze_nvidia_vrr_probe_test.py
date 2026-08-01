#!/usr/bin/env python3
"""Exercise the pure M14 NVIDIA probe analyzer and its gw-hw command."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "gw-hw.d"))
from nvidia_probe_analysis import (  # noqa: E402
    PROBE_SCHEMA, analyze_probe,
)


CONFIG_TEXT = '''drm_device = "/dev/dri/card0"
required_base_commit = "6864ea631d61636289a21c7d2d6655a17be0c004"
tested_commit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
connector = "DP-1"
mode = "2560x1440@120000"
tty = "/dev/tty2"
alternate_tty = "/dev/tty1"
keyboard_device = "/dev/input/event0"
pointer_device = "/dev/input/event1"
expected_min_refresh_hz = 48
expected_max_refresh_hz = 144
target_refresh_hz = 70
monitor_model = "fixture monitor"
edid_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
debugfs_connector_path = "/sys/kernel/debug/dri/0/DP-1"
'''
CONFIG = {
    "connector": "DP-1", "mode": "2560x1440@120000",
    "target_refresh_hz": 70,
}


def records(off_interval: int = 8_333_333,
            on_interval: int = 14_285_714) -> list[dict[str, object]]:
    run_id = "1" * 32
    values: list[dict[str, object]] = [{
        "schema": PROBE_SCHEMA, "record": "probe-start", "run_id": run_id,
        "connector": "DP-1", "crtc_id": 77,
        "mode": "2560x1440@120000", "hardware_capable": True,
        "atomic_test_off": True, "atomic_test_on": True,
        "target_refresh_hz": 70, "warmup_flips": 10,
        "recorded_flips": 140, "restore_confirmation_flips": 1,
    }]
    token = 1
    for phase, interval, start in (
            ("off", off_interval, 1_000_000_000),
            ("on", on_interval, 10_000_000_000)):
        prior: int | None = None
        for ordinal in range(150):
            timestamp = (start + interval * ordinal) // 1_000 * 1_000
            values.append({
                "schema": PROBE_SCHEMA, "record": "flip", "run_id": run_id,
                "phase": phase, "ordinal": ordinal,
                "sample_kind": "warmup" if ordinal < 10 else "recorded",
                "framebuffer_id": 100 + token % 2,
                "atomic_request_token": token,
                "requested_vrr_enabled": phase == "on",
                "readback_vrr_enabled": phase == "on", "crtc_id": 77,
                "raw_event_sequence": 0, "raw_event_seconds": timestamp // 1_000_000_000,
                "raw_event_microseconds": timestamp % 1_000_000_000 // 1_000,
                "raw_kernel_timestamp_nanoseconds": timestamp,
                "raw_timestamp_available": True,
                "raw_timestamp_invalid_reason": "",
                "raw_interval_nanoseconds": (
                    None if prior is None else timestamp - prior),
                "transition_serial": 1 if phase == "off" else 2,
                "submit_deadline_nanoseconds": timestamp - 2_000_000,
                "submit_monotonic_timestamp_nanoseconds": timestamp - 1_000_000,
                "completion_dequeue_timestamp_nanoseconds": timestamp + 1_000_000,
                "crtc_sequence_query": {"source": "crtc", "eligible": False},
                "legacy_vblank_query": {"source": "legacy", "eligible": False},
                "atomic_status": "success", "atomic_errno": 0,
                "property_readback_status": "success",
            })
            prior = timestamp
            token += 1
    timestamp = 20_000_000_000
    values.append({
        "schema": PROBE_SCHEMA, "record": "flip", "run_id": run_id,
        "phase": "restore-off", "ordinal": 0, "sample_kind": "warmup",
        "framebuffer_id": 100 + token % 2, "atomic_request_token": token,
        "requested_vrr_enabled": False, "readback_vrr_enabled": False,
        "crtc_id": 77, "raw_event_sequence": 0,
        "raw_event_seconds": timestamp // 1_000_000_000,
        "raw_event_microseconds": 0,
        "raw_kernel_timestamp_nanoseconds": timestamp,
        "raw_timestamp_available": True, "raw_timestamp_invalid_reason": "",
        "raw_interval_nanoseconds": None, "transition_serial": 3,
        "submit_deadline_nanoseconds": timestamp - 2_000_000,
        "submit_monotonic_timestamp_nanoseconds": timestamp - 1_000_000,
        "completion_dequeue_timestamp_nanoseconds": timestamp + 1_000_000,
        "crtc_sequence_query": {}, "legacy_vblank_query": {},
        "atomic_status": "success", "atomic_errno": 0,
        "property_readback_status": "success",
    })
    values.append({
        "schema": PROBE_SCHEMA, "record": "restore", "run_id": run_id,
        "kms_state_equal": True, "vrr_property_restored": True,
        "passed": True, "error": "",
    })
    return values


def write_report(path: Path, values: list[dict[str, object]]) -> None:
    path.write_text("".join(json.dumps(value, sort_keys=True) + "\n"
                            for value in values), encoding="utf-8")


def rejected(path: Path) -> None:
    try:
        analyze_probe(path, CONFIG)
    except ValueError:
        return
    raise AssertionError("invalid NVIDIA probe report was accepted")


def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        report = root / "probe.jsonl"
        write_report(report, records())
        accepted = analyze_probe(report, CONFIG)
        assert accepted["classification"] == "accepted-probe-distinction"
        assert accepted["passed"] is True
        assert accepted["phases"]["off"]["sample_count"] == 140
        assert accepted["phases"]["on"]["pass_percentage"] == 100.0
        assert accepted["query_diagnostics_used_for_acceptance"] is False

        forced = root / "forced.jsonl"
        write_report(forced, records(on_interval=14_285_714,
                                     off_interval=14_285_714))
        assert analyze_probe(forced, CONFIG)["classification"] == \
            "off-variable-on-variable"

        rejected_test = records()
        rejected_test[0]["atomic_test_on"] = False
        rejected_test = [rejected_test[0], rejected_test[-1]]
        rejected_test_path = root / "atomic-on-rejected.jsonl"
        write_report(rejected_test_path, rejected_test)
        assert analyze_probe(rejected_test_path, CONFIG)["classification"] == \
            "atomic-on-rejected"

        failed_submission = records()
        failed_submission[1]["atomic_status"] = "failed"
        failed_submission[1]["raw_timestamp_available"] = False
        failed_submission[1]["raw_timestamp_invalid_reason"] = \
            "unavailable-or-regressed"
        failed_submission[1]["raw_kernel_timestamp_nanoseconds"] = 0
        failed_submission[1]["raw_event_seconds"] = 0
        failed_submission[1]["raw_event_microseconds"] = 0
        failed_submission[1]["raw_interval_nanoseconds"] = None
        failed_submission = [failed_submission[0], failed_submission[1],
                             failed_submission[-1]]
        failed_submission_path = root / "atomic-off-failed.jsonl"
        write_report(failed_submission_path, failed_submission)
        assert analyze_probe(
            failed_submission_path, CONFIG)["classification"] == \
            "atomic-off-rejected"

        regressed_values = records()
        first_on = next(index for index, value in enumerate(regressed_values)
                        if value.get("record") == "flip" and
                        value.get("phase") == "on" and
                        value.get("ordinal") == 20)
        prior = regressed_values[first_on - 1]["raw_kernel_timestamp_nanoseconds"]
        regressed_values[first_on]["raw_kernel_timestamp_nanoseconds"] = prior
        regressed_values[first_on]["raw_event_seconds"] = prior // 1_000_000_000
        regressed_values[first_on]["raw_event_microseconds"] = \
            prior % 1_000_000_000 // 1_000
        regressed_values[first_on]["raw_interval_nanoseconds"] = 1
        regressed = root / "regressed.jsonl"
        write_report(regressed, regressed_values)
        rejected(regressed)

        mixed = records()
        mixed[151]["crtc_id"] = 88
        mixed_path = root / "mixed.jsonl"
        write_report(mixed_path, mixed)
        rejected(mixed_path)

        config = root / "config.toml"
        config.write_text(CONFIG_TEXT, encoding="utf-8")
        output = root / "summary.json"
        result = subprocess.run([
            sys.executable, str(ROOT / "tools" / "gw-hw"),
            "analyze-milestone14-nvidia-vrr-probe",
            "--report", str(report), "--config", str(config),
            "--output", str(output),
        ], check=False, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        assert result.returncode == 0, result.stderr
        assert json.loads(output.read_text(encoding="utf-8"))["passed"] is True
        repeated = subprocess.run(result.args, check=False, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert repeated.returncode == 1 and "cannot create" in repeated.stderr
    print("m14 NVIDIA probe analyzer test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
