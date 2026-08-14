"""Deterministic evidence builders for gw-hw's injected self-test only."""

from __future__ import annotations

import json
from pathlib import Path


def _write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")


def populate_live_evidence(
        root: Path, config: dict[str, object]) -> dict[str, tuple[int, int]]:
    root.mkdir(mode=0o700)
    _write(root / "milestone14-build-provenance.json", {
        "schema": "glasswyrm.m14-build-provenance.v1",
        "source_commit": config["tested_commit"],
        "tracked_source_clean": True,
        "binaries": [
            {"role": role, "path": path, "size": index + 1,
            "sha256": f"{index + 1:064x}"}
            for index, (role, path) in enumerate((
                ("libgwipc", "src/libgwipc.so.0.9.0"),
                ("gwm", "src/gwm"),
                ("gwcomp", "src/gwcomp"),
                ("server", "src/glasswyrmd"),
                ("gwout", "tools/gwout"),
                ("gwinfo", "tools/gwinfo"),
                ("client", "tests/manifest/m14/m14_vrr_client"),
                ("drm-probe", "tools/gw_drm_probe"),
                ("drm-vrr-probe", "tools/gw_drm_vrr_probe"),
            ))
        ],
    })
    _write(root / "milestone14-hardware-doctor.json", {
        "passed": True,
        "required_base_commit": config["required_base_commit"],
        "tested_commit": config["tested_commit"],
    })
    _write(root / "milestone14-hardware-config.json", config)
    _write(root / "milestone14-drm-capability.json", {"controllable": True})
    kms = {"connector": config["connector"], "mode": config["mode"],
           "crtc": 77, "vrr_enabled": False}
    _write(root / "kms-before.json", kms)
    _write(root / "kms-after.json", kms)

    records: list[dict[str, object]] = [{
        "record": "vrr-capability", "connector": config["connector"],
        "controllable": True, "atomic_test_off": True, "atomic_test_on": True,
    }]
    scenarios: list[str | None] = [None]
    drm_records: list[dict[str, object]] = []
    token = 1

    def append_presentation(record: dict[str, object],
                            scenario: str | None = None) -> None:
        nonlocal token
        identity = {
            "output_id": 1, "commit_id": token, "generation": token,
            "presentation_token": token,
        }
        record.update(commit_id=token, generation=token)
        records.extend([
            {"record": "evidence-stream", **identity, "stream": 2},
            record,
            {"record": "evidence-seal", **identity, "required_streams": 3,
             "committed_streams": 3, "mirror_frame": 0,
             "mirror_fnv1a64": "0000000000000000", "mirror_file": ""},
        ])
        scenarios.extend([scenario, scenario, scenario])
        drm_records.extend([
            {"record": "evidence-stream", **identity, "stream": 1},
            {"record": "flip", "commit_id": token, "generation": token},
        ])
        token += 1

    for policy in ("off", "fullscreen", "focused", "app-requested",
                   "always-eligible"):
        append_presentation(
            {"record": "vrr-decision", "policy": policy,
             "effective_enabled": policy != "off", "session_active": True})
    append_presentation(
        {"record": "vrr-decision", "policy": "always-eligible",
         "effective_enabled": False, "session_active": False})
    for enabled, interval, start in ((False, 6_944_444, 100),
                                     (True, 14_285_714, 1000)):
        timestamp = 1_000_000_000 if not enabled else 10_000_000_000
        for index in range(131):
            append_presentation(
                {"record": "vrr-timing", "sequence": start + index,
                 "kernel_timestamp_nanoseconds": timestamp + interval * index,
                 "effective_enabled": enabled},
                "on-cadence" if enabled else "off-cadence")
    records.append({"record": "vrr-restore", "original_enabled": False,
                    "restored_enabled": False, "readback_success": True,
                    "kms_restore": True, "vt_restore": True,
                    "getty_restore": True})
    scenarios.append(None)
    cadence_ranges: dict[str, tuple[int, int]] = {}
    offset = 0
    with (root / "vrr-part-1.jsonl").open("w", encoding="utf-8") as output:
        for record, scenario in zip(records, scenarios, strict=True):
            line = json.dumps(record, sort_keys=True) + "\n"
            if scenario is not None:
                previous = cadence_ranges.get(scenario, (offset, offset))
                cadence_ranges[scenario] = (
                    previous[0], offset + len(line.encode("utf-8")))
            output.write(line)
            offset += len(line.encode("utf-8"))
    with (root / "milestone14-drm-report.jsonl").open(
            "w", encoding="utf-8") as output:
        for record in drm_records:
            output.write(json.dumps(record, sort_keys=True) + "\n")
    base_client = {
        "schema": "glasswyrm.m14-vrr-client.v3", "mode": "windowed",
        "window": 100, "width": 640, "height": 480,
        "preference": "Default", "fullscreen_requested": False,
        "borderless": False, "frame_count": 1, "target_refresh_hz": 70,
        "target_interval_nanoseconds": 14_285_714,
        "eventfd_synchronized": False, "events_selected": True,
        "preference_reply_count": 0, "notify_event_count": 0,
        "notify_change_mask": 0, "reason_mask": 0,
        "preference_sequence": [], "cadence_absolute_monotonic": False,
        "bounded_damage_width": 64, "bounded_damage_height": 64,
        "selected_output": "", "scheduled_frame_count": 0,
        "submitted_frame_count": 0, "presented_frame_count": 0,
        "first_observed_commit_id": 0, "last_observed_commit_id": 0,
        "first_presented_generation": 0, "last_presented_generation": 0,
        "maximum_outstanding_updates": 0, "retryable_query_count": 0,
        "missed_deadline_count": 0,
        "maximum_completion_latency_nanoseconds": 0,
        "presentation_paced": False,
    }
    for tag in ("off-cadence", "on-cadence"):
        value = dict(
            base_client, mode="cadence", width=2560, height=1440,
            fullscreen_requested=True, frame_count=180,
            eventfd_synchronized=True, cadence_absolute_monotonic=True,
            selected_output=config["connector"], scheduled_frame_count=180,
            submitted_frame_count=180, presented_frame_count=180,
            first_observed_commit_id=1, last_observed_commit_id=180,
            first_presented_generation=1, last_presented_generation=180,
            maximum_outstanding_updates=1,
            maximum_completion_latency_nanoseconds=14_285_714,
            presentation_paced=True,
        )
        _write(root / f"client-{tag}.json", value)
    preference = dict(base_client, mode="preference", window=101,
                      preference="Disable", preference_reply_count=4,
                      notify_event_count=3, notify_change_mask=1,
                      reason_mask=(1 << 19) | (1 << 20),
                      preference_sequence=["Default", "Allow", "Prefer", "Disable"])
    _write(root / "client-app-preferences.json", preference)
    app_default = dict(base_client, window=102, preference_reply_count=1,
                       notify_event_count=0, notify_change_mask=0,
                       reason_mask=1 << 20)
    app_prefer = dict(base_client, mode="app-requested", window=103,
                      preference="Prefer", preference_reply_count=1,
                      notify_event_count=1, notify_change_mask=1,
                      reason_mask=1)
    _write(root / "client-app-default.json", app_default)
    _write(root / "client-app-prefer.json", app_prefer)
    _write(root / "client-focus-a.json", dict(base_client, window=201))
    _write(root / "client-focus-b.json", dict(base_client, window=202))

    output = {"name": config["connector"], "policy": "focused",
              "hardware_capable": True, "kms_controllable": True,
              "simulated": False, "effective_enabled": True}
    _write(root / "milestone14-focused.log",
           {"vrr": [dict(output, candidate_window=201)], "windows": []})
    _write(root / "milestone14-focused-transfer.json",
           {"vrr": [dict(output, candidate_window=202)], "windows": []})
    app_output = dict(output, policy="app-requested")
    _write(root / "milestone14-app-requested-default.json", {
        "vrr": [dict(app_output, effective_enabled=False, candidate_window=0,
                     reasons=["no-candidate"])],
        "windows": [{"window": 102, "preference": "Default",
                     "reasons": ["window-did-not-request"]}],
    })
    _write(root / "milestone14-app-requested.log", {
        "vrr": [dict(app_output, effective_enabled=True, candidate_window=103,
                     reasons=[])],
        "windows": [{"window": 103, "preference": "Prefer", "reasons": []}],
    })
    _write(root / "milestone14-app-requested-disable.json", {
        "vrr": [dict(app_output, effective_enabled=False, candidate_window=0,
                     reasons=["no-candidate"])],
        "windows": [{"window": 101, "preference": "Disable",
                     "reasons": ["window-preference-disabled",
                                 "window-did-not-request"]}],
    })
    for name in ("milestone14-fullscreen.log", "milestone14-borderless.log",
                 "milestone14-always.log",
                 "milestone14-vt.log"):
        _write(root / name, {"passed": True})
    replay = {"vrr": [{"name": config["connector"],
                        "policy": "always-eligible",
                        "effective_enabled": True}]}
    _write(root / "milestone14-restart-gwm.json", replay)
    _write(root / "milestone14-restart.log", replay)
    _write(root / "milestone14-capture-off-state.json", {
        "vrr": [dict(output, policy="off", effective_enabled=False,
                     candidate_window=0)]})
    _write(root / "milestone14-capture-enabled-state.json", {
        "vrr": [dict(output, policy="always-eligible", effective_enabled=True,
                     candidate_window=0)]})
    ppm = b"P6\n1 1\n255\n\x12\x34\x56"
    (root / "milestone14-canonical.ppm").write_bytes(ppm)
    (root / "milestone14-screen.ppm").write_bytes(ppm)
    frames = root / "frames"
    frames.mkdir()
    with (frames / "frames.jsonl").open("w", encoding="utf-8") as output:
        for frame in (10_001, 10_002):
            output.write(json.dumps({
                "frame": frame, "commit_id": frame, "generation": frame,
                "output_id": 1, "width": 1, "height": 1,
                "damage_rectangles": 0, "fnv1a64": "7486b218c3c86edf",
                "file": f"frame-{frame}.ppm",
            }, sort_keys=True) + "\n")
    return cadence_ranges
