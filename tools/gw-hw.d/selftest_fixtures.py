"""Deterministic evidence builders for gw-hw's injected self-test only."""

from __future__ import annotations

import json
from pathlib import Path


def _write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")


def populate_live_evidence(root: Path, config: dict[str, object]) -> None:
    root.mkdir(mode=0o700)
    _write(root / "milestone14-hardware-doctor.json", {"passed": True})
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
    for policy in ("off", "fullscreen", "focused", "app-requested",
                   "always-eligible"):
        records.append({"record": "vrr-decision", "policy": policy,
                        "effective_enabled": policy != "off",
                        "session_active": True})
    records.append({"record": "vrr-decision", "policy": "always-eligible",
                    "effective_enabled": False, "session_active": False})
    for enabled, interval, start in ((False, 6_944_444, 100),
                                     (True, 14_285_714, 1000)):
        timestamp = 1_000_000_000 if not enabled else 10_000_000_000
        for index in range(131):
            records.append({"record": "vrr-timing", "sequence": start + index,
                            "kernel_timestamp_nanoseconds": timestamp + interval * index,
                            "effective_enabled": enabled})
    records.append({"record": "vrr-restore", "original_enabled": False,
                    "restored_enabled": False, "readback_success": True,
                    "kms_restore": True, "vt_restore": True,
                    "getty_restore": True})
    with (root / "vrr-part-1.jsonl").open("w", encoding="utf-8") as output:
        for record in records:
            output.write(json.dumps(record, sort_keys=True) + "\n")

    base_client = {
        "schema": "glasswyrm.m14-vrr-client.v2", "mode": "windowed",
        "window": 100, "preference": "Default", "frame_count": 1,
        "eventfd_synchronized": False, "events_selected": True,
    }
    for tag in ("off-cadence", "on-cadence"):
        value = dict(base_client, mode="cadence", frame_count=180,
                     eventfd_synchronized=True)
        _write(root / f"client-{tag}.json", value)
    preference = dict(base_client, mode="preference", window=101,
                      preference="Disable", preference_reply_count=4,
                      notify_event_count=3, notify_change_mask=1, reason_mask=32,
                      preference_sequence=["Default", "Allow", "Prefer", "Disable"])
    _write(root / "client-app-preferences.json", preference)
    _write(root / "client-focus-a.json", dict(base_client, window=201))
    _write(root / "client-focus-b.json", dict(base_client, window=202))

    output = {"name": config["connector"], "policy": "focused",
              "hardware_capable": True, "kms_controllable": True,
              "simulated": False, "effective_enabled": True}
    _write(root / "milestone14-focused.log",
           {"vrr": [dict(output, candidate_window=201)], "windows": []})
    _write(root / "milestone14-focused-transfer.json",
           {"vrr": [dict(output, candidate_window=202)], "windows": []})
    for name in ("milestone14-fullscreen.log", "milestone14-borderless.log",
                 "milestone14-app-requested.log", "milestone14-always.log",
                 "milestone14-vt.log"):
        _write(root / name, {"passed": True})
    replay = {"vrr": [{"name": config["connector"],
                        "policy": "always-eligible",
                        "effective_enabled": True}]}
    _write(root / "milestone14-restart-gwm.json", replay)
    _write(root / "milestone14-restart.log", replay)
    ppm = b"P6\n1 1\n255\n\x12\x34\x56"
    (root / "milestone14-canonical.ppm").write_bytes(ppm)
    (root / "milestone14-screen.ppm").write_bytes(ppm)
