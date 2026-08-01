"""Pure offline analysis for the bounded M14 NVIDIA VRR probe."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
from statistics import median_low
from typing import Any

from common import (
    ENABLED_PASS_PERCENT, DISABLED_PASS_PERCENT, HarnessError,
    MAX_JSON_BYTES, MIN_ENABLED_INTERVALS, MODE_PATTERN, _read_regular,
    interval_tolerance,
)


PROBE_SCHEMA = "glasswyrm.m14-nvidia-vrr-probe.v1"
SUMMARY_SCHEMA = "glasswyrm.m14-nvidia-vrr-probe-summary.v1"
MAX_PROBE_RECORDS = 1024
PHASES = ("off", "on")
START_KEYS = {
    "schema", "record", "run_id", "connector", "crtc_id", "mode",
    "hardware_capable", "atomic_test_off", "atomic_test_on",
    "target_refresh_hz", "warmup_flips", "recorded_flips",
    "restore_confirmation_flips",
}
FLIP_KEYS = {
    "schema", "record", "run_id", "phase", "ordinal", "sample_kind",
    "framebuffer_id", "atomic_request_token", "requested_vrr_enabled",
    "readback_vrr_enabled", "crtc_id", "raw_event_sequence",
    "raw_event_seconds", "raw_event_microseconds",
    "raw_kernel_timestamp_nanoseconds", "raw_timestamp_available",
    "raw_timestamp_invalid_reason", "raw_interval_nanoseconds",
    "transition_serial", "submit_deadline_nanoseconds",
    "submit_monotonic_timestamp_nanoseconds",
    "completion_dequeue_timestamp_nanoseconds", "crtc_sequence_query",
    "legacy_vblank_query", "atomic_status", "atomic_errno",
    "property_readback_status",
}
RESTORE_KEYS = {
    "schema", "record", "run_id", "kms_state_equal",
    "vrr_property_restored", "passed", "error",
}


def _integer(value: object, name: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise HarnessError(f"probe {name} must be an integer >= {minimum}")
    return value


def _boolean(value: object, name: str) -> bool:
    if not isinstance(value, bool):
        raise HarnessError(f"probe {name} must be Boolean")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], record: str) -> None:
    if set(value) != expected:
        missing = ", ".join(sorted(expected - set(value))) or "none"
        unknown = ", ".join(sorted(set(value) - expected)) or "none"
        raise HarnessError(
            f"probe {record} schema mismatch; missing: {missing}; "
            f"unknown: {unknown}")


def _load_records(path: Path) -> list[dict[str, Any]]:
    try:
        text = _read_regular(path, MAX_JSON_BYTES).decode("utf-8")
    except UnicodeError as error:
        raise HarnessError(f"invalid {path.name}: {error}") from error
    if not text.endswith("\n"):
        raise HarnessError("probe report has an unclosed final record")
    records: list[dict[str, Any]] = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line:
            raise HarnessError(f"probe report contains an empty line at {number}")
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise HarnessError(
                f"invalid probe JSONL line {number}: {error}") from error
        if not isinstance(value, dict):
            raise HarnessError(f"probe JSONL line {number} is not an object")
        if any("wall" in key.lower() or "realtime" in key.lower()
               for key in value):
            raise HarnessError("probe report contains a wall-clock field")
        records.append(value)
        if len(records) > MAX_PROBE_RECORDS:
            raise HarnessError("probe report exceeds the record bound")
    if not records:
        raise HarnessError("probe report is empty")
    return records


def _distribution(intervals: list[int], target: float,
                  tolerance: float, nominal: float) -> dict[str, object]:
    ordered = sorted(intervals)
    within = sum(abs(value - target) <= tolerance for value in intervals)
    quantized = 0
    for value in intervals:
        multiple = max(1, round(value / nominal))
        if abs(value - multiple * nominal) <= tolerance:
            quantized += 1
    percentage = 100.0 * within / len(intervals) if intervals else 0.0
    return {
        "sample_count": len(intervals),
        "within_tolerance_count": within,
        "pass_percentage": round(percentage, 6),
        "fixed_refresh_quantized_count": quantized,
        "minimum_nanoseconds": ordered[0] if ordered else 0,
        "maximum_nanoseconds": ordered[-1] if ordered else 0,
        "mean_nanoseconds": sum(ordered) // len(ordered) if ordered else 0,
        "median_nanoseconds": median_low(ordered) if ordered else 0,
    }


def analyze_probe(path: Path, config: dict[str, object]) -> dict[str, object]:
    """Validate and classify one complete probe report."""
    records = _load_records(path)
    start = records[0]
    restore = records[-1]
    _exact_keys(start, START_KEYS, "start")
    _exact_keys(restore, RESTORE_KEYS, "restore")
    if (start.get("schema") != PROBE_SCHEMA or
            start.get("record") != "probe-start"):
        raise HarnessError("probe start record is unsupported")
    if (restore.get("schema") != PROBE_SCHEMA or
            restore.get("record") != "restore"):
        raise HarnessError("probe report is not closed by restoration evidence")
    run_id = start.get("run_id")
    if (not isinstance(run_id, str) or
            re.fullmatch(r"[0-9a-f]{32}", run_id) is None):
        raise HarnessError("probe run_id must be 32 lowercase hex digits")
    if restore.get("run_id") != run_id:
        raise HarnessError("probe restoration run identity diverged")
    connector = start.get("connector")
    mode = start.get("mode")
    if connector != config.get("connector") or mode != config.get("mode"):
        raise HarnessError("probe connector or mode differs from configuration")
    target_hz = _integer(start.get("target_refresh_hz"), "target refresh", 1)
    if target_hz != config.get("target_refresh_hz"):
        raise HarnessError("probe target refresh differs from configuration")
    crtc_id = _integer(start.get("crtc_id"), "CRTC ID", 1)
    warmup_flips = _integer(start.get("warmup_flips"), "warmup flips")
    recorded_flips = _integer(start.get("recorded_flips"), "recorded flips", 1)
    if warmup_flips > 64 or recorded_flips > 512:
        raise HarnessError("probe flip count exceeds the bounded profile")
    hardware_capable = _boolean(
        start.get("hardware_capable"), "hardware capability")
    atomic_test_off = _boolean(start.get("atomic_test_off"), "atomic off test")
    atomic_test_on = _boolean(start.get("atomic_test_on"), "atomic on test")

    restore_confirmation_flips = _integer(
        start.get("restore_confirmation_flips"), "restore confirmation flips", 1)
    if restore_confirmation_flips != 1:
        raise HarnessError("probe requires one off-restore confirmation flip")
    phase_timestamps: dict[str, int | None] = {phase: None for phase in PHASES}
    intervals: dict[str, list[int]] = {phase: [] for phase in PHASES}
    recorded_counts = {phase: 0 for phase in PHASES}
    lost_counts = {phase: 0 for phase in PHASES}
    ordinals: dict[str, set[int]] = {phase: set() for phase in PHASES}
    readback_diverged = False
    atomic_failures: set[bool] = set()
    expected_framebuffer: int | None = None
    restore_ordinals: set[int] = set()
    for value in records[1:-1]:
        _exact_keys(value, FLIP_KEYS, "flip")
        if value.get("schema") != PROBE_SCHEMA or value.get("record") != "flip":
            raise HarnessError("probe contains a non-flip body record")
        if value.get("run_id") != run_id:
            raise HarnessError("probe flip run identity diverged")
        phase = value.get("phase")
        if phase not in {*PHASES, "restore-off"}:
            raise HarnessError("probe flip phase is invalid")
        if _integer(value.get("crtc_id"), "flip CRTC ID", 1) != crtc_id:
            raise HarnessError("probe CRTC identity changed")
        ordinal = _integer(value.get("ordinal"), "flip ordinal")
        phase_ordinals = (restore_ordinals if phase == "restore-off"
                          else ordinals[phase])
        if ordinal in phase_ordinals:
            raise HarnessError("probe contains a duplicate phase ordinal")
        phase_ordinals.add(ordinal)
        framebuffer = _integer(value.get("framebuffer_id"), "framebuffer ID", 1)
        if expected_framebuffer == framebuffer:
            raise HarnessError("probe did not alternate framebuffer IDs")
        expected_framebuffer = framebuffer
        sample_kind = value.get("sample_kind")
        if sample_kind not in {"warmup", "recorded"}:
            raise HarnessError("probe sample_kind is invalid")
        requested = _boolean(
            value.get("requested_vrr_enabled"), "requested VRR state")
        readback = _boolean(
            value.get("readback_vrr_enabled"), "readback VRR state")
        if requested != (phase == "on"):
            raise HarnessError("probe phase and requested VRR state diverged")
        if (readback != requested or
                value.get("property_readback_status") != "success"):
            readback_diverged = True
        atomic_status = value.get("atomic_status")
        atomic_errno = _integer(value.get("atomic_errno"), "atomic errno")
        if atomic_status not in {"success", "failed"}:
            raise HarnessError("probe atomic status is invalid")
        if atomic_status == "success" and atomic_errno != 0:
            raise HarnessError("successful atomic probe record has an errno")
        if atomic_status == "failed":
            atomic_failures.add(requested)
        for name in ("atomic_request_token", "raw_event_sequence",
                     "transition_serial", "submit_deadline_nanoseconds",
                     "submit_monotonic_timestamp_nanoseconds",
                     "completion_dequeue_timestamp_nanoseconds"):
            _integer(value.get(name), name)
        for name in ("crtc_sequence_query", "legacy_vblank_query"):
            if not isinstance(value.get(name), dict):
                raise HarnessError(f"probe {name} diagnostic is not an object")
        raw_available = _boolean(
            value.get("raw_timestamp_available"), "raw timestamp availability")
        raw_timestamp = _integer(
            value.get("raw_kernel_timestamp_nanoseconds"), "raw timestamp")
        seconds = _integer(value.get("raw_event_seconds"), "raw event seconds")
        microseconds = _integer(
            value.get("raw_event_microseconds"), "raw event microseconds")
        if microseconds >= 1_000_000:
            raise HarnessError("probe raw event microseconds are invalid")
        raw_interval = value.get("raw_interval_nanoseconds")
        if raw_interval is not None:
            _integer(raw_interval, "raw interval", 1)
        reason = value.get("raw_timestamp_invalid_reason")
        if not isinstance(reason, str):
            raise HarnessError("probe raw timestamp reason is not text")
        previous = None if phase == "restore-off" else phase_timestamps[phase]
        if raw_available:
            if reason or raw_timestamp == 0:
                raise HarnessError("available raw timestamp is marked invalid")
            if raw_timestamp != seconds * 1_000_000_000 + microseconds * 1_000:
                raise HarnessError("raw timestamp does not match event fields")
            if previous is not None and raw_timestamp <= previous:
                raise HarnessError("probe raw timestamp regressed")
            expected_interval = None if previous is None else raw_timestamp - previous
            if raw_interval != expected_interval:
                raise HarnessError("probe raw interval does not match event timestamps")
            if phase != "restore-off":
                phase_timestamps[phase] = raw_timestamp
            if (phase != "restore-off" and sample_kind == "recorded" and
                    expected_interval is not None):
                intervals[phase].append(expected_interval)
        else:
            if raw_timestamp != 0 or raw_interval is not None or not reason:
                raise HarnessError("unavailable raw timestamp lacks exact degradation")
            if phase != "restore-off" and sample_kind == "recorded":
                lost_counts[phase] += 1
        if phase != "restore-off" and sample_kind == "recorded":
            recorded_counts[phase] += 1

    expected_per_phase = warmup_flips + recorded_flips
    incomplete_probe = (not hardware_capable or not atomic_test_off or
                        not atomic_test_on or bool(atomic_failures) or
                        readback_diverged)
    if not incomplete_probe:
        if any(ordinals[phase] != set(range(expected_per_phase))
               for phase in PHASES):
            raise HarnessError(
                "probe phase ordinals differ from the contiguous declaration")
        if restore_ordinals != {0}:
            raise HarnessError("probe off-restore confirmation is incomplete")
        if any(recorded_counts[phase] != recorded_flips for phase in PHASES):
            raise HarnessError(
                "probe recorded sample count differs from its declaration")
    restored = (_boolean(restore.get("passed"), "restore result") and
                _boolean(restore.get("kms_state_equal"), "KMS restoration") and
                _boolean(restore.get("vrr_property_restored"), "VRR restoration"))
    if not isinstance(restore.get("error"), str):
        raise HarnessError("probe restoration error is not text")

    mode_match = MODE_PATTERN.fullmatch(str(mode))
    assert mode_match is not None
    nominal = 1_000_000_000_000.0 / int(mode_match.group(3))
    target = 1_000_000_000.0 / target_hz
    tolerance = interval_tolerance(target)
    distributions = {
        phase: _distribution(intervals[phase], target, tolerance, nominal)
        for phase in PHASES
    }
    if not restored:
        classification = "restore-failed"
    elif not hardware_capable:
        classification = "capability-unavailable"
    elif not atomic_test_off or False in atomic_failures:
        classification = "atomic-off-rejected"
    elif not atomic_test_on or True in atomic_failures:
        classification = "atomic-on-rejected"
    elif readback_diverged:
        classification = "readback-diverged"
    elif not intervals["off"] or not intervals["on"]:
        classification = "timing-unavailable"
    elif any(len(intervals[phase]) < MIN_ENABLED_INTERVALS for phase in PHASES):
        classification = "insufficient-samples"
    else:
        off_fixed = (distributions["off"]["pass_percentage"] <
                     DISABLED_PASS_PERCENT)
        on_variable = (distributions["on"]["pass_percentage"] >=
                       ENABLED_PASS_PERCENT)
        if off_fixed and on_variable:
            classification = "accepted-probe-distinction"
        elif not off_fixed and on_variable:
            classification = "off-variable-on-variable"
        elif off_fixed and not on_variable:
            classification = "off-fixed-on-fixed"
        else:
            classification = "off-variable-on-fixed"
    return {
        "schema": SUMMARY_SCHEMA,
        "classification": classification,
        "passed": classification == "accepted-probe-distinction",
        "run_id": run_id,
        "connector": connector,
        "crtc_id": crtc_id,
        "mode": mode,
        "target_refresh_hz": target_hz,
        "target_interval_nanoseconds": round(target),
        "tolerance_nanoseconds": round(tolerance),
        "raw_page_flip_timestamps_authoritative": True,
        "query_diagnostics_used_for_acceptance": False,
        "recorded_flip_count": recorded_counts,
        "raw_timestamp_unavailable_count": lost_counts,
        "phases": distributions,
        "restoration_passed": restored,
    }


def write_summary_exclusive(path: Path, value: dict[str, object]) -> None:
    """Create one analysis result without replacing an existing artifact."""
    contents = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    try:
        descriptor = os.open(
            path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    except OSError as error:
        raise HarnessError(f"cannot create {path.name}: {error}") from error
    try:
        written = 0
        while written < len(contents):
            count = os.write(descriptor, contents[written:])
            if count <= 0:
                raise HarnessError(f"cannot write {path.name}")
            written += count
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
