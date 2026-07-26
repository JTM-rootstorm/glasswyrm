"""Cadence, restoration, and deterministic evidence archive validation."""

from __future__ import annotations

from collections import deque
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import tarfile
from typing import Any

from common import (
    ARCHIVE_STATE_ARTIFACTS, ARTIFACT_SCHEMA, BUILD_PROVENANCE_ARTIFACT,
    COMMIT_PATTERN,
    DISABLED_PASS_PERCENT,
    ENABLED_PASS_PERCENT, FIXTURE_COPY_ARTIFACTS, HarnessError,
    M14_REQUIRED_BASE_COMMIT, MAX_ARTIFACT_BYTES, MAX_INTERVALS, MAX_JSON_BYTES,
    MIN_ENABLED_INTERVALS, MODE_PATTERN, REQUIRED_ARTIFACTS, RUN_STEPS,
    _read_json, _read_regular, _write_json, interval_tolerance,
    vrr_rejection_reasons,
)
from provenance import validate_archived_provenance

_DRM_EVIDENCE_STREAM = 1
_VRR_EVIDENCE_STREAM = 2
_MIRROR_EVIDENCE_STREAM = 4
_KNOWN_EVIDENCE_STREAMS = (
    _DRM_EVIDENCE_STREAM | _VRR_EVIDENCE_STREAM | _MIRROR_EVIDENCE_STREAM)


def _ppm_rgb_fnv1a64(path: Path) -> str:
    contents = _read_regular(path, MAX_ARTIFACT_BYTES)
    header = re.match(rb"P6\n([1-9][0-9]*) ([1-9][0-9]*)\n255\n", contents)
    if header is None:
        raise HarnessError(f"{path.name} is not a canonical binary PPM")
    width = int(header.group(1))
    height = int(header.group(2))
    rgb = contents[header.end():]
    if len(rgb) != width * height * 3:
        raise HarnessError(f"{path.name} pixel payload size is invalid")
    digest = 14695981039346656037
    for byte in rgb:
        digest = ((digest ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{digest:016x}"


def drm_evidence_streams(
        records: list[dict[str, Any]]) -> set[tuple[int, int, int, int]]:
    identities: set[tuple[int, int, int, int]] = set()
    for record in records:
        if not isinstance(record, dict):
            raise HarnessError("DRM report contains a non-object record")
        if record.get("record") != "evidence-stream":
            continue
        values = tuple(record.get(name) for name in (
            "output_id", "commit_id", "generation", "presentation_token"))
        if (any(isinstance(value, bool) or not isinstance(value, int) or
                value <= 0 for value in values) or
                record.get("stream") != _DRM_EVIDENCE_STREAM or
                values in identities):
            raise HarnessError("DRM evidence stream identity is invalid")
        identities.add(values)  # type: ignore[arg-type]
    return identities


def mirror_evidence_streams(
        records: list[dict[str, Any]]
        ) -> dict[tuple[int, int, int, int], tuple[str, str]]:
    identities: dict[tuple[int, int, int, int], tuple[str, str]] = {}
    for record in records:
        if not isinstance(record, dict):
            raise HarnessError("mirror report contains a non-object record")
        values = tuple(record.get(name) for name in (
            "output_id", "commit_id", "generation", "frame"))
        digest = record.get("fnv1a64")
        file_name = record.get("file")
        if (any(isinstance(value, bool) or not isinstance(value, int) or
                value <= 0 for value in values) or
                not isinstance(digest, str) or
                not re.fullmatch(r"[0-9a-f]{16}", digest) or
                not isinstance(file_name, str) or not file_name or
                Path(file_name).name != file_name or
                values in identities):
            raise HarnessError("mirror evidence stream identity is invalid")
        identities[values] = (digest, file_name)  # type: ignore[index]
    return identities


def sealed_vrr_records(
        records: list[dict[str, Any]], require_seals: bool,
        drm_streams: set[tuple[int, int, int, int]] | None = None,
        mirror_streams: dict[
            tuple[int, int, int, int], tuple[str, str]] | None = None,
        ) -> list[dict[str, Any]]:
    """Return global records and presentation records completed by a valid seal."""
    evidence_present = any(
        isinstance(record, dict) and
        record.get("record") in {"evidence-stream", "evidence-seal"}
        for record in records)
    if not evidence_present:
        if require_seals:
            raise HarnessError("VRR report contains no presentation evidence seals")
        return records

    pending: dict[tuple[int, int, int, int], list[dict[str, Any]]] = {}
    current: tuple[int, int, int, int] | None = None
    accepted: list[dict[str, Any]] = []
    sealed: set[tuple[int, int, int, int]] = set()
    for record in records:
        if not isinstance(record, dict):
            raise HarnessError("VRR report contains a non-object record")
        kind = record.get("record")
        if kind == "evidence-stream":
            values = tuple(record.get(name) for name in (
                "output_id", "commit_id", "generation", "presentation_token"))
            if (len(values) != 4 or
                    any(isinstance(value, bool) or not isinstance(value, int) or
                        value <= 0 for value in values) or
                    record.get("stream") != _VRR_EVIDENCE_STREAM):
                raise HarnessError("VRR evidence stream identity is invalid")
            current = values  # type: ignore[assignment]
            if current in pending or current in sealed:
                raise HarnessError("VRR evidence stream identity is duplicated")
            pending[current] = []
            continue
        if kind in {"vrr-decision", "vrr-timing"}:
            if current is None:
                raise HarnessError("VRR presentation record has no evidence stream")
            if (record.get("commit_id"), record.get("generation")) != current[1:3]:
                raise HarnessError("VRR presentation record identity does not match")
            pending[current].append(record)
            continue
        if kind == "evidence-seal":
            values = tuple(record.get(name) for name in (
                "output_id", "commit_id", "generation", "presentation_token"))
            required = record.get("required_streams")
            committed = record.get("committed_streams")
            if (len(values) != 4 or
                    any(isinstance(value, bool) or not isinstance(value, int) or
                        value <= 0 for value in values) or
                    values != current or values not in pending or
                    values in sealed or isinstance(required, bool) or
                    not isinstance(required, int) or
                    required & ~_KNOWN_EVIDENCE_STREAMS or
                    required & (_DRM_EVIDENCE_STREAM | _VRR_EVIDENCE_STREAM) !=
                    (_DRM_EVIDENCE_STREAM | _VRR_EVIDENCE_STREAM) or
                    committed != required):
                raise HarnessError("VRR presentation evidence seal is invalid")
            if drm_streams is not None and values not in drm_streams:
                raise HarnessError(
                    "VRR presentation seal has no matching DRM evidence stream")
            mirror_required = bool(required & _MIRROR_EVIDENCE_STREAM)
            mirror_frame = record.get("mirror_frame")
            mirror_hash = record.get("mirror_fnv1a64")
            mirror_file = record.get("mirror_file")
            if mirror_required:
                if (isinstance(mirror_frame, bool) or
                        not isinstance(mirror_frame, int) or mirror_frame <= 0 or
                        not isinstance(mirror_hash, str) or len(mirror_hash) != 16 or
                        not isinstance(mirror_file, str) or not mirror_file or
                        Path(mirror_file).name != mirror_file):
                    raise HarnessError("sealed mirror evidence identity is invalid")
                mirror_identity = (*values[:3], mirror_frame)
                if (mirror_streams is not None and
                        mirror_streams.get(mirror_identity) !=
                        (mirror_hash, mirror_file)):
                    raise HarnessError(
                        "VRR presentation seal has no matching mirror evidence stream")
            elif mirror_frame != 0 or mirror_hash != "0000000000000000" or mirror_file != "":
                raise HarnessError("non-mirror evidence seal carries mirror fields")
            if not pending[values]:
                raise HarnessError("VRR evidence seal contains no presentation records")
            accepted.extend(pending.pop(values))
            sealed.add(values)
            current = None
            continue
        accepted.append(record)
    return accepted


def analyze_cadence(
        report_path: Path, config: dict[str, object], enabled: bool,
        source_range: tuple[int, int] | None = None,
        scenario: str | None = None,
        require_seals: bool = False,
        drm_report_path: Path | None = None) -> dict[str, object]:
    contents = _read_regular(report_path, MAX_JSON_BYTES)
    if source_range is None:
        start, end = 0, len(contents)
    else:
        start, end = source_range
        if (isinstance(start, bool) or not isinstance(start, int) or
                isinstance(end, bool) or not isinstance(end, int) or
                start < 0 or start >= end or end > len(contents) or
                (start > 0 and contents[start - 1:start] != b"\n") or
                contents[end - 1:end] != b"\n"):
            raise HarnessError("cadence source byte range is invalid or unaligned")
    lines = contents[start:end].decode("utf-8").splitlines()
    parsed: list[dict[str, Any]] = []
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise HarnessError(f"invalid VRR report JSONL line {number}: {error}") from error
        if not isinstance(record, dict):
            raise HarnessError(f"invalid VRR report record at line {number}")
        parsed.append(record)
    drm_streams = None
    if drm_report_path is not None:
        drm_records = [
            json.loads(line) for line in
            _read_regular(drm_report_path, MAX_JSON_BYTES)
            .decode("utf-8").splitlines() if line.strip()]
        if not all(isinstance(record, dict) for record in drm_records):
            raise HarnessError("DRM report contains a non-object record")
        drm_streams = drm_evidence_streams(drm_records)
    parsed = sealed_vrr_records(parsed, require_seals, drm_streams)

    intervals: deque[int] = deque(maxlen=MAX_INTERVALS)
    previous: dict[int, tuple[int, int]] = {}
    for number, record in enumerate(parsed, 1):
        if record.get("record") not in {"vrr-timing", "timing"}:
            continue
        crtc = record.get("crtc_id", record.get("output_id", 0))
        if record.get("effective_enabled") is not enabled:
            if isinstance(crtc, int) and not isinstance(crtc, bool):
                previous.pop(crtc, None)
            continue
        sequence = record.get("sequence")
        timestamp = record.get("kernel_timestamp_nanoseconds")
        if (isinstance(crtc, bool) or not isinstance(crtc, int) or crtc < 0 or
                isinstance(sequence, bool) or not isinstance(sequence, int) or not 0 <= sequence <= 0xffffffff or
                isinstance(timestamp, bool) or not isinstance(timestamp, int) or timestamp < 0):
            raise HarnessError(f"invalid timing record at line {number}")
        if timestamp == 0:
            previous.pop(crtc, None)
            continue
        prior = previous.get(crtc)
        previous[crtc] = (sequence, timestamp)
        if prior is None:
            continue
        if timestamp <= prior[1]:
            raise HarnessError(f"kernel timestamp regression at line {number}")
        sequence_delta = (sequence - prior[0]) & 0xffffffff
        if sequence_delta == 0 and sequence == 0:
            intervals.append(timestamp - prior[1])
            continue
        if sequence_delta == 0 or sequence_delta >= (1 << 31):
            continue
        intervals.append(timestamp - prior[1])
    target_ns = 1_000_000_000.0 / int(config["target_refresh_hz"])
    tolerance_ns = interval_tolerance(target_ns)
    values = list(intervals)
    errors = sorted(abs(value - target_ns) for value in values)
    within = sum(abs(value - target_ns) <= tolerance_ns for value in values)
    percentage = 100.0 * within / len(values) if values else 0.0
    ordered = sorted(values)
    def percentile95(items: list[float]) -> float:
        return items[min(len(items) - 1, math.ceil(len(items) * .95) - 1)] if items else 0.0
    median = ((ordered[(len(ordered) - 1) // 2] + ordered[len(ordered) // 2]) / 2.0) if ordered else 0.0
    mode_match = MODE_PATTERN.fullmatch(str(config["mode"]))
    assert mode_match
    fixed_ns = 1_000_000_000_000.0 / int(mode_match.group(3))
    quantized = sum(abs(value - max(1, round(value / fixed_ns)) * fixed_ns) <= 250_000 for value in values)
    enabled_criterion = len(values) >= MIN_ENABLED_INTERVALS and percentage >= ENABLED_PASS_PERCENT
    passed = (enabled_criterion if enabled else
              len(values) >= MIN_ENABLED_INTERVALS and
              percentage < DISABLED_PASS_PERCENT and not enabled_criterion)
    result: dict[str, object] = {
        "schema": ARTIFACT_SCHEMA, "state": "enabled" if enabled else "disabled",
        "sample_count": len(values), "target_interval_ns": round(target_ns, 3),
        "tolerance_ns": round(tolerance_ns, 3), "within_threshold_count": within,
        "pass_percentage": round(percentage, 6), "minimum_ns": min(values) if values else 0,
        "maximum_ns": max(values) if values else 0,
        "mean_ns": round(sum(values) / len(values), 3) if values else 0,
        "median_ns": round(median, 3), "p95_absolute_error_ns": round(percentile95(errors), 3),
        "fixed_quantized_count": quantized, "property_readback": 1 if enabled else 0,
        "passed": passed,
    }
    if source_range is not None:
        result["source_byte_range"] = {"start": start, "end": end}
    if scenario is not None:
        result["scenario"] = scenario
    return result


def validate_restore(before_path: Path, after_path: Path) -> dict[str, object]:
    before = _read_json(before_path)
    after = _read_json(after_path)
    required = {"vrr_enabled", "kms", "kd_mode", "active_vt", "getty_active"}
    if set(before) != required or set(after) != required:
        raise HarnessError("restore snapshots must use the exact state schema")
    fields = {key: before[key] == after[key] for key in sorted(required)}
    return {"schema": ARTIFACT_SCHEMA, "original": before, "restored": after,
            "fields": fields, "readback_success": all(fields.values()),
            "passed": all(fields.values())}


def _validate_app_requested_snapshot(
        path: Path, config: dict[str, object], client: dict[str, Any],
        preference: str, effective: bool, output_reasons: list[str],
        window_reasons: list[str]) -> None:
    snapshot = _read_json(path)
    outputs = snapshot.get("vrr")
    windows = snapshot.get("windows")
    if (not isinstance(outputs, list) or len(outputs) != 1 or
            not isinstance(outputs[0], dict) or
            outputs[0].get("name") != config["connector"] or
            outputs[0].get("policy") != "app-requested" or
            outputs[0].get("effective_enabled") is not effective or
            outputs[0].get("hardware_capable") is not True or
            outputs[0].get("kms_controllable") is not True or
            outputs[0].get("simulated") is not False or
            vrr_rejection_reasons(outputs[0].get("reasons")) != output_reasons or
            not isinstance(windows, list)):
        raise HarnessError(f"{path.name} omitted authoritative AppRequested output state")
    window = next((item for item in windows if isinstance(item, dict) and
                   item.get("window") == client.get("window")), None)
    if (window is None or window.get("preference", "").lower() != preference or
            window.get("reasons") != window_reasons):
        raise HarnessError(f"{path.name} omitted authoritative {preference} window state")
    candidate = outputs[0].get("candidate_window")
    expected_candidate = client.get("window") if effective else 0
    if candidate != expected_candidate:
        raise HarnessError(f"{path.name} has the wrong AppRequested candidate")


def finalize_live(config: dict[str, object], artifacts: Path,
                  runner: Any) -> None:
    report = artifacts / "milestone14-vrr-report.jsonl"
    parts = [path for path in (artifacts / "vrr-part-0.jsonl",
                               artifacts / "vrr-part-1.jsonl") if path.is_file()]
    if not parts:
        raise HarnessError("VRR report parts are missing")
    report.write_bytes(b"".join(_read_regular(path, MAX_JSON_BYTES) for path in parts))
    drm_report = artifacts / "milestone14-drm-report.jsonl"
    drm_parts = [
        path for path in (artifacts / "drm-part-0.jsonl", drm_report)
        if path.is_file()]
    if not drm_parts:
        raise HarnessError("DRM report parts are missing")
    drm_report.write_bytes(
        b"".join(_read_regular(path, MAX_JSON_BYTES) for path in drm_parts))
    mirror_report = artifacts / "milestone14-mirror-report.jsonl"
    mirror_report.write_bytes(_read_regular(
        artifacts / "frames" / "frames.jsonl", MAX_JSON_BYTES))
    off_range = runner.cadence_ranges.get("off-cadence")
    on_range = runner.cadence_ranges.get("on-cadence")
    if (off_range is None or on_range is None or
            off_range[1] > on_range[0]):
        raise HarnessError("ordered cadence scenario boundaries are incomplete")
    off = analyze_cadence(
        report, config, False, off_range, "off-cadence", require_seals=True,
        drm_report_path=drm_report)
    on = analyze_cadence(
        report, config, True, on_range, "on-cadence", require_seals=True,
        drm_report_path=drm_report)
    _write_json(artifacts / "milestone14-vrr-off-summary.json", off)
    _write_json(artifacts / "milestone14-vrr-on-summary.json", on)
    if not off["passed"] or not on["passed"]:
        raise HarnessError("live cadence acceptance failed")
    records: list[dict[str, Any]] = []
    for line in _read_regular(report, MAX_JSON_BYTES).decode("utf-8").splitlines():
        if line.strip():
            value = json.loads(line)
            if not isinstance(value, dict):
                raise HarnessError("VRR report contains a non-object record")
            records.append(value)
    drm_records = [
        json.loads(line) for line in
        _read_regular(drm_report, MAX_JSON_BYTES).decode("utf-8").splitlines()
        if line.strip()]
    mirror_records = [
        json.loads(line) for line in
        _read_regular(mirror_report, MAX_JSON_BYTES).decode("utf-8").splitlines()
        if line.strip()]
    records = sealed_vrr_records(
        records, require_seals=True, drm_streams=drm_evidence_streams(
            drm_records), mirror_streams=mirror_evidence_streams(
                mirror_records))
    capabilities = [record for record in records if record.get("record") == "vrr-capability"]
    if not capabilities or not any(record.get("controllable") is True and
            record.get("connector") == config["connector"] and
            record.get("atomic_test_off") is True and
            record.get("atomic_test_on") is True for record in capabilities):
        raise HarnessError("positive hardware capability record is missing")
    decisions = [record for record in records if record.get("record") == "vrr-decision"]
    policies = {record.get("policy") for record in decisions}
    if not {"off", "fullscreen", "focused", "app-requested",
            "always-eligible"}.issubset(policies):
        raise HarnessError("VRR decision report omits required policies")
    if not any(record.get("effective_enabled") is True for record in decisions) or not any(
            record.get("effective_enabled") is False for record in decisions):
        raise HarnessError("VRR decision report omits enabled/disabled transitions")
    if not any(record.get("session_active") is False and
               record.get("effective_enabled") is False for record in decisions):
        raise HarnessError("VRR report omits inactive-session disable evidence")
    restore_records = [record for record in records if record.get("record") == "vrr-restore"]
    if not restore_records:
        raise HarnessError("VRR restore record is missing")
    value = restore_records[-1]
    if runner.before_state is None or runner.after_state is None:
        raise HarnessError("console before/after state is incomplete")
    before_kms = _read_json(artifacts / "kms-before.json")
    after_kms = _read_json(artifacts / "kms-after.json")
    restore_checks = {
        "vrr": value.get("readback_success") is True and
               value.get("original_enabled") == value.get("restored_enabled"),
        "kms": value.get("kms_restore") is True and before_kms == after_kms,
        "active_vt": value.get("vt_restore") is True and
              runner.before_state.get("active_vt") == runner.after_state.get("active_vt"),
        "kd_mode": value.get("vt_restore") is True and
              runner.before_state.get("kd_mode") == runner.after_state.get("kd_mode"),
        "getty": value.get("getty_restore") is True and
                 runner.before_state.get("getty_active") == runner.after_state.get("getty_active"),
        "cleanup": not runner.cleanup_errors,
    }
    restore_errors = list(runner.cleanup_errors)
    for name, passed in restore_checks.items():
        if passed:
            continue
        if name in {"active_vt", "kd_mode", "getty"}:
            field = "getty_active" if name == "getty" else name
            restore_errors.append(
                f"{name} restoration mismatch: expected "
                f"{runner.before_state.get(field)}, observed "
                f"{runner.after_state.get(field)}")
        else:
            restore_errors.append(f"restoration check failed: {name}")
    restore_errors = list(dict.fromkeys(restore_errors))
    restore = {"schema": ARTIFACT_SCHEMA,
               "original_value": value.get("original_enabled"),
               "restored_value": value.get("restored_enabled"),
               "original_console": runner.before_state,
               "restored_console": runner.after_state,
               "checks": restore_checks, "errors": restore_errors,
               "readback_success": all(restore_checks.values()),
               "passed": all(restore_checks.values()) and not restore_errors}
    _write_json(artifacts / "milestone14-restore.json", restore)
    _write_json(artifacts / ARCHIVE_STATE_ARTIFACTS[0],
                {"kms": before_kms, "vrr_enabled": value.get("original_enabled"),
                 **runner.before_state})
    _write_json(artifacts / ARCHIVE_STATE_ARTIFACTS[1],
                {"kms": after_kms, "vrr_enabled": value.get("restored_enabled"),
                 **runner.after_state})
    if not restore["passed"]:
        raise HarnessError("live exact restoration validation failed")
    for tag in ("off-cadence", "on-cadence"):
        state = _read_json(artifacts / f"client-{tag}.json")
        presented = state.get("presented_frame_count")
        if (state.get("schema") != "glasswyrm.m14-vrr-client.v3" or
                state.get("mode") != "cadence" or
                state.get("frame_count") != 180 or
                state.get("eventfd_synchronized") is not True or
                state.get("events_selected") is not True or
                state.get("selected_output") != config["connector"] or
                state.get("presentation_paced") is not True or
                state.get("scheduled_frame_count") != 180 or
                state.get("submitted_frame_count") != 180 or
                type(presented) is not int or presented < 121 or presented > 180 or
                state.get("maximum_outstanding_updates") != 1 or
                type(state.get("first_observed_commit_id")) is not int or
                state["first_observed_commit_id"] <= 0 or
                type(state.get("last_observed_commit_id")) is not int or
                state["last_observed_commit_id"] <=
                state["first_observed_commit_id"] or
                type(state.get("first_presented_generation")) is not int or
                state["first_presented_generation"] <= 0 or
                type(state.get("last_presented_generation")) is not int or
                state["last_presented_generation"] <=
                state["first_presented_generation"] or
                type(state.get("maximum_completion_latency_nanoseconds"))
                is not int or
                state["maximum_completion_latency_nanoseconds"] <= 0):
            raise HarnessError(
                f"{tag} omitted presentation-paced cadence evidence")
    preference = _read_json(artifacts / "client-app-preferences.json")
    if (preference.get("schema") != "glasswyrm.m14-vrr-client.v3" or
            preference.get("mode") != "preference" or
            preference.get("preference") != "Disable" or
            preference.get("preference_sequence") !=
                ["Default", "Allow", "Prefer", "Disable"] or
            preference.get("preference_reply_count") != 4 or
            not isinstance(preference.get("notify_event_count"), int) or
            preference["notify_event_count"] < 3 or
            preference.get("notify_change_mask", 0) & 1 == 0 or
            not isinstance(preference.get("reason_mask"), int) or
            preference["reason_mask"] != ((1 << 19) | (1 << 20))):
        raise HarnessError("preference client omitted replies/events/reasons")
    default_client = _read_json(artifacts / "client-app-default.json")
    prefer_client = _read_json(artifacts / "client-app-prefer.json")
    if (default_client.get("preference") != "Default" or
            default_client.get("preference_reply_count") != 1 or
            default_client.get("reason_mask") != (1 << 20) or
            prefer_client.get("preference") != "Prefer" or
            prefer_client.get("preference_reply_count") != 1 or
            not isinstance(prefer_client.get("notify_change_mask"), int) or
            prefer_client["notify_change_mask"] == 0):
        raise HarnessError("AppRequested clients omitted reviewed preferences")
    _validate_app_requested_snapshot(
        artifacts / "milestone14-app-requested-default.json", config,
        default_client, "default", False, ["no-candidate"],
        ["window-did-not-request"])
    _validate_app_requested_snapshot(
        artifacts / "milestone14-app-requested.log", config,
        prefer_client, "prefer", True, [], [])
    _validate_app_requested_snapshot(
        artifacts / "milestone14-app-requested-disable.json", config,
        preference, "disable", False, ["no-candidate"],
        ["window-preference-disabled", "window-did-not-request"])
    focus_a = _read_json(artifacts / "client-focus-a.json")
    focus_b = _read_json(artifacts / "client-focus-b.json")
    first_focus = _read_json(artifacts / "milestone14-focused.log")
    second_focus = _read_json(artifacts / "milestone14-focused-transfer.json")
    first_candidate = first_focus["vrr"][0].get("candidate_window")
    second_candidate = second_focus["vrr"][0].get("candidate_window")
    if (focus_a.get("window") == focus_b.get("window") or
            first_candidate != focus_a.get("window") or
            second_candidate != focus_b.get("window")):
        raise HarnessError("Focused policy did not transfer between two windows")
    for replay_name in ("milestone14-restart-gwm.json", "milestone14-restart.log"):
        replay = _read_json(artifacts / replay_name)
        outputs = replay.get("vrr")
        if (not isinstance(outputs, list) or len(outputs) != 1 or
                outputs[0].get("policy") != "always-eligible" or
                outputs[0].get("effective_enabled") is not True):
            raise HarnessError(f"{replay_name} omitted exact enabled replay evidence")
    for capture_name, policy, effective in (
            ("milestone14-capture-off-state.json", "off", False),
            ("milestone14-capture-enabled-state.json", "always-eligible", True)):
        capture = _read_json(artifacts / capture_name)
        outputs = capture.get("vrr")
        if (not isinstance(outputs, list) or len(outputs) != 1 or
                outputs[0].get("name") != config["connector"] or
                outputs[0].get("policy") != policy or
                outputs[0].get("effective_enabled") is not effective or
                outputs[0].get("hardware_capable") is not True or
                outputs[0].get("kms_controllable") is not True or
                outputs[0].get("simulated") is not False):
            raise HarnessError(
                f"{capture_name} omitted authoritative capture state")
    if not (artifacts / "milestone14-canonical.ppm").is_file() or not (
            artifacts / "milestone14-screen.ppm").is_file():
        raise HarnessError("canonical screen evidence is missing")
    if (artifacts / "milestone14-canonical.ppm").read_bytes() != (artifacts / "milestone14-screen.ppm").read_bytes():
        raise HarnessError("canonical and final screen pixels differ")
    expected_mirror_hash = _ppm_rgb_fnv1a64(
        artifacts / "milestone14-canonical.ppm")
    mirror_streams = mirror_evidence_streams(mirror_records)
    if (len(mirror_streams) != 2 or
            any(digest != expected_mirror_hash
                for digest, _ in mirror_streams.values())):
        raise HarnessError(
            "sealed mirror reports do not match both canonical pixel captures")
    for name in ("milestone14-fullscreen.log", "milestone14-borderless.log",
                 "milestone14-focused.log", "milestone14-app-requested.log",
                 "milestone14-always.log", "milestone14-vt.log", "milestone14-restart.log"):
        if not (artifacts / name).is_file() or (artifacts / name).stat().st_size == 0:
            raise HarnessError(f"required live scenario evidence is missing: {name}")
    summary = {"schema": ARTIFACT_SCHEMA, "dry_run": False, "passed": True,
               "required_base_commit": config["required_base_commit"],
               "tested_commit": config["tested_commit"],
               "run_step_count": len(runner.steps), "failure_stage": None,
               "evidence_errors": [], "enabled_pass_percentage": on["pass_percentage"],
               "disabled_pass_percentage": off["pass_percentage"],
               "restoration": True, "archive_validation": True}
    _write_json(artifacts / "milestone14-hardware-summary.json", summary)
    archive = _create_archive(artifacts)
    validation = validate_archive(artifacts, archive)
    if not validation["passed"]:
        raise HarnessError("; ".join(validation["errors"]))


def _copy_fixture_artifacts(fixture_dir: Path, artifact_dir: Path) -> None:
    for name in FIXTURE_COPY_ARTIFACTS:
        source = fixture_dir / name
        contents = _read_regular(source, MAX_ARTIFACT_BYTES)
        (artifact_dir / name).write_bytes(contents)


def _hashes(artifact_dir: Path) -> dict[str, str]:
    return {name: hashlib.sha256((artifact_dir / name).read_bytes()).hexdigest()
            for name in (*REQUIRED_ARTIFACTS, *ARCHIVE_STATE_ARTIFACTS)}


def _create_archive(artifact_dir: Path) -> Path:
    hashes = _hashes(artifact_dir)
    sums = "".join(f"{digest}  {name}\n" for name, digest in sorted(hashes.items()))
    (artifact_dir / "SHA256SUMS").write_text(sums, encoding="ascii")
    archive = artifact_dir / "milestone14-vrr-hardware-evidence.tar"
    with tarfile.open(archive, "w", format=tarfile.PAX_FORMAT) as output:
        for name in sorted((*REQUIRED_ARTIFACTS, *ARCHIVE_STATE_ARTIFACTS, "SHA256SUMS")):
            path = artifact_dir / name
            info = output.gettarinfo(path, arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = 0
            with path.open("rb") as stream:
                output.addfile(info, stream)
    return archive


def validate_archive(artifact_dir: Path, archive: Path) -> dict[str, object]:
    missing = [name for name in (*REQUIRED_ARTIFACTS, *ARCHIVE_STATE_ARTIFACTS)
               if not (artifact_dir / name).is_file()]
    errors: list[str] = [f"missing artifact: {name}" for name in missing]
    if not missing:
        config = _read_json(artifact_dir / "milestone14-hardware-config.json")
        doctor = _read_json(artifact_dir / "milestone14-hardware-doctor.json")
        summary = _read_json(artifact_dir / "milestone14-hardware-summary.json")
        required_base = config.get("required_base_commit")
        tested_commit = config.get("tested_commit")
        if (required_base != M14_REQUIRED_BASE_COMMIT or
                not isinstance(tested_commit, str) or
                not COMMIT_PATTERN.fullmatch(tested_commit)):
            errors.append("reviewed source identity is invalid")
        if isinstance(tested_commit, str):
            try:
                validate_archived_provenance(
                    artifact_dir / BUILD_PROVENANCE_ARTIFACT, tested_commit)
            except HarnessError as error:
                errors.append(f"build provenance is invalid: {error}")
        for label, record in (("doctor", doctor), ("summary", summary)):
            if (record.get("required_base_commit") != required_base or
                    record.get("tested_commit") != tested_commit):
                errors.append(f"{label} source identity does not match configuration")
        mirror_records: list[dict[str, Any]] = []
        if summary.get("dry_run") is False:
            try:
                vrr_records = [
                    json.loads(line) for line in _read_regular(
                        artifact_dir / "milestone14-vrr-report.jsonl",
                        MAX_JSON_BYTES).decode("utf-8").splitlines()
                    if line.strip()]
                drm_records = [
                    json.loads(line) for line in _read_regular(
                        artifact_dir / "milestone14-drm-report.jsonl",
                        MAX_JSON_BYTES).decode("utf-8").splitlines()
                    if line.strip()]
                mirror_records = [
                    json.loads(line) for line in _read_regular(
                        artifact_dir / "milestone14-mirror-report.jsonl",
                        MAX_JSON_BYTES).decode("utf-8").splitlines()
                    if line.strip()]
                sealed_vrr_records(
                    vrr_records, require_seals=True,
                    drm_streams=drm_evidence_streams(drm_records),
                    mirror_streams=mirror_evidence_streams(mirror_records))
            except (HarnessError, json.JSONDecodeError) as error:
                errors.append(f"sealed presentation evidence is invalid: {error}")
            for name, enabled, scenario in (
                    ("milestone14-vrr-off-summary.json", False, "off-cadence"),
                    ("milestone14-vrr-on-summary.json", True, "on-cadence")):
                cadence = _read_json(artifact_dir / name)
                source_range = cadence.get("source_byte_range")
                if (not isinstance(source_range, dict) or
                        set(source_range) != {"start", "end"}):
                    errors.append(f"{name} omits its exact scenario byte range")
                    continue
                try:
                    recomputed = analyze_cadence(
                        artifact_dir / "milestone14-vrr-report.jsonl", config,
                        enabled,
                        (source_range["start"], source_range["end"]),
                        scenario, require_seals=True,
                        drm_report_path=artifact_dir /
                        "milestone14-drm-report.jsonl")
                    if cadence != recomputed:
                        errors.append(f"{name} differs from bounded report evidence")
                except HarnessError as error:
                    errors.append(f"{name} has invalid bounded evidence: {error}")
        if (artifact_dir / "milestone14-canonical.ppm").read_bytes() != (artifact_dir / "milestone14-screen.ppm").read_bytes():
            errors.append("canonical and screen PPM differ")
        if summary.get("dry_run") is False:
            try:
                expected_mirror_hash = _ppm_rgb_fnv1a64(
                    artifact_dir / "milestone14-canonical.ppm")
                archived_mirrors = mirror_evidence_streams(mirror_records)
                if (len(archived_mirrors) != 2 or
                        any(digest != expected_mirror_hash
                            for digest, _ in archived_mirrors.values())):
                    errors.append(
                        "archived mirror seals do not match canonical pixels")
            except HarnessError as error:
                errors.append(f"archived mirror evidence is invalid: {error}")
        expected = set(REQUIRED_ARTIFACTS) | set(ARCHIVE_STATE_ARTIFACTS) | {"SHA256SUMS"}
        try:
            with tarfile.open(archive, "r:") as source:
                names = source.getnames()
                if set(names) != expected or len(names) != len(expected):
                    errors.append("archive member set is not exact")
                for member in source.getmembers():
                    if not member.isfile() or "/" in member.name or member.name.startswith("."):
                        errors.append(f"unsafe archive member: {member.name}")
                        continue
                    stream = source.extractfile(member)
                    if stream is None or stream.read(MAX_ARTIFACT_BYTES + 1) != (
                            artifact_dir / member.name).read_bytes():
                        errors.append(f"archive member differs from evidence: {member.name}")
        except (OSError, tarfile.TarError) as error:
            errors.append(f"archive unreadable: {error}")
        sums = (artifact_dir / "SHA256SUMS").read_text(encoding="ascii").splitlines()
        expected_sums = [f"{digest}  {name}" for name, digest in sorted(_hashes(artifact_dir).items())]
        if sums != expected_sums:
            errors.append("SHA256SUMS mismatch")
    return {"passed": not errors, "errors": errors}
