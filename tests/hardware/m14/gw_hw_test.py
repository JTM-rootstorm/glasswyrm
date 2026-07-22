#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "gw-hw.d"))
from config_doctor import (  # noqa: E402
    ConfigError, _connector_profile, _modetest_commands,
    _parse_debugfs_refresh_range, _reviewed_range_source, parse_config,
)

TOOL = ROOT / "tools" / "gw-hw"
CONFIG_TEXT = '''drm_device = "/dev/dri/card0"
required_base_commit = "6864ea631d61636289a21c7d2d6655a17be0c004"
tested_commit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
connector = "DP-1"
mode = "2560x1440@144000"
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
TEXT_ARTIFACTS = (
    "milestone14-fullscreen.log", "milestone14-borderless.log",
    "milestone14-focused.log",
    "milestone14-always.log", "milestone14-vt.log", "milestone14-restart.log",
)
REQUIRED_BASE = "6864ea631d61636289a21c7d2d6655a17be0c004"
TESTED_COMMIT = "b" * 40


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(TOOL), *arguments], check=False,
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")


def make_fixture(root: Path, restored: bool = True) -> tuple[Path, Path]:
    fixture = root / "fixture"
    fixture.mkdir()
    config = root / "config.toml"
    config.write_text(CONFIG_TEXT, encoding="utf-8")
    facts = {
        "schema": "glasswyrm.m14-hardware.v1", "root": True,
        "drm_device": "/dev/dri/card0", "drm_primary_node": True,
        "tty": "/dev/tty2", "tty_character_device": True,
        "tty_kd_text": True, "active_tty": "/dev/tty2", "spare_tty": True,
        "alternate_tty": "/dev/tty1", "alternate_tty_character_device": True,
        "alternate_tty_kd_text": True, "alternate_tty_safe": True,
        "connector": "DP-1", "connected": True, "active": True,
        "connected_connector_count": 1, "active_connector_count": 1,
        "single_connected_active_connector": True,
        "edid_sha256": "a" * 64,
        "vrr_capable": 1, "atomic_kms": True, "vrr_enabled_property": True,
        "mode": "2560x1440@144000", "selected_mode_available": True,
        "range_source": "debugfs",
        "minimum_refresh_hz": 48, "maximum_refresh_hz": 144,
        "no_competing_drm_master": True, "session_permissions": True,
        "kernel": "fixture-kernel", "libdrm": "fixture-libdrm",
        "driver": "fixture-driver", "firmware": "fixture-firmware",
        "keyboard_device": "/dev/input/event0",
        "pointer_device": "/dev/input/event1",
        "keyboard_character_device": True, "pointer_character_device": True,
    }
    write_json(fixture / "doctor.json", facts)
    write_json(fixture / "milestone14-build-provenance.json", {
        "schema": "glasswyrm.m14-build-provenance.v1",
        "source_commit": TESTED_COMMIT,
        "tracked_source_clean": True,
        "binaries": [
            {"role": role, "path": path, "size": index + 1,
             "sha256": f"{index + 1:064x}"}
            for index, (role, path) in enumerate((
                ("gwm", "src/gwm"),
                ("gwcomp", "src/gwcomp"),
                ("server", "src/glasswyrmd"),
                ("gwout", "tools/gwout"),
                ("gwinfo", "tools/gwinfo"),
                ("client", "tests/manifest/m14/m14_vrr_client"),
                ("drm-probe", "tools/gw_drm_probe"),
            ))
        ],
    })
    before = {"vrr_enabled": 0, "kms": {"mode": "2560x1440@144000"},
              "kd_mode": "text", "active_vt": 1, "getty_active": True}
    after = dict(before)
    if not restored:
        after["getty_active"] = False
    write_json(fixture / "state-before.json", before)
    write_json(fixture / "state-after.json", after)
    records: list[dict[str, object]] = []
    for enabled, interval, start_sequence in ((False, 6_944_444, 0xfffffff0),
                                               (True, 14_285_714, 200)):
        timestamp = 1_000_000_000 if not enabled else 10_000_000_000
        for index in range(131):
            records.append({"record": "vrr-timing", "crtc_id": 77,
                            "sequence": (start_sequence + index) & 0xffffffff,
                            "kernel_timestamp_nanoseconds": timestamp + interval * index,
                            "effective_enabled": enabled})
    with (fixture / "milestone14-vrr-report.jsonl").open("w", encoding="utf-8") as output:
        for record in records:
            output.write(json.dumps(record, sort_keys=True) + "\n")
    for name in TEXT_ARTIFACTS:
        (fixture / name).write_text(f"fixture {name}\n", encoding="utf-8")
    client = {
        "schema": "glasswyrm.m14-vrr-client.v2", "mode": "windowed",
        "window": 101, "preference": "Default", "frame_count": 1,
        "eventfd_synchronized": False, "events_selected": True,
    }
    write_json(fixture / "client-app-default.json",
               dict(client, preference_reply_count=1, notify_event_count=0,
                    notify_change_mask=0, reason_mask=1 << 20))
    write_json(fixture / "client-app-prefer.json",
               dict(client, window=102, mode="app-requested", preference="Prefer",
                    preference_reply_count=1, notify_event_count=1,
                    notify_change_mask=1, reason_mask=0))
    write_json(fixture / "client-app-preferences.json",
               dict(client, window=103, mode="preference", preference="Disable",
                    preference_reply_count=4, notify_event_count=3,
                    notify_change_mask=1, reason_mask=1 << 19,
                    preference_sequence=["Default", "Allow", "Prefer", "Disable"]))
    app_output = {
        "name": "DP-1", "policy": "app-requested",
        "hardware_capable": True, "kms_controllable": True,
        "simulated": False,
    }
    write_json(fixture / "milestone14-app-requested-default.json", {
        "vrr": [dict(app_output, effective_enabled=False, candidate_window=0,
                     reasons=["no-candidate"])],
        "windows": [{"window": 101, "preference": "Default",
                     "reasons": ["window-did-not-request"]}],
    })
    write_json(fixture / "milestone14-app-requested.log", {
        "vrr": [dict(app_output, effective_enabled=True, candidate_window=102,
                     reasons=[])],
        "windows": [{"window": 102, "preference": "Prefer", "reasons": []}],
    })
    write_json(fixture / "milestone14-app-requested-disable.json", {
        "vrr": [dict(app_output, effective_enabled=False, candidate_window=0,
                     reasons=["no-candidate"])],
        "windows": [{"window": 103, "preference": "Disable",
                     "reasons": ["window-preference-disabled"]}],
    })
    capture_output = dict(app_output, candidate_window=0)
    write_json(fixture / "milestone14-capture-off-state.json", {
        "vrr": [dict(capture_output, policy="off", effective_enabled=False)],
    })
    write_json(fixture / "milestone14-capture-enabled-state.json", {
        "vrr": [dict(capture_output, policy="always-eligible",
                     effective_enabled=True)],
    })
    ppm = b"P6\n1 1\n255\n\x12\x34\x56"
    (fixture / "milestone14-canonical.ppm").write_bytes(ppm)
    (fixture / "milestone14-screen.ppm").write_bytes(ppm)
    return config, fixture


def assert_archive(artifacts: Path) -> None:
    archive = artifacts / "milestone14-vrr-hardware-evidence.tar"
    assert archive.is_file() and archive.stat().st_size > 0
    sums = (artifacts / "SHA256SUMS").read_text(encoding="ascii").splitlines()
    for line in sums:
        digest, name = line.split("  ", 1)
        assert hashlib.sha256((artifacts / name).read_bytes()).hexdigest() == digest


def main() -> int:
    assert _modetest_commands(
        "/usr/bin/modetest", Path("/dev/dri/card0"), "nvidia") == [
            ["/usr/bin/modetest", "-D", "/dev/dri/card0", "-c", "-e", "-p"],
            ["/usr/bin/modetest", "-M", "nvidia-drm", "-D",
             "/dev/dri/card0", "-c", "-e", "-p"],
        ]
    assert _modetest_commands(
        "/usr/bin/modetest", Path("/dev/dri/card1"), "amdgpu")[-1] == [
            "/usr/bin/modetest", "-M", "amdgpu", "-D", "/dev/dri/card1",
            "-c", "-e", "-p",
        ]
    assert len(_modetest_commands(
        "/usr/bin/modetest", Path("/dev/dri/card0"), "bad/module")) == 1
    assert _parse_debugfs_refresh_range("Min: 48 Hz\nMax: 144 Hz\n") == (48, 144)
    assert _parse_debugfs_refresh_range(
        "minimum_refresh = 48 maximum_refresh = 144") == (48, 144)
    assert _parse_debugfs_refresh_range("vrr_capable: 1") is None
    assert _parse_debugfs_refresh_range("min: 48 min: 60 max: 144") is None
    assert _reviewed_range_source("min: 48 max: 144", 48, 144) == "debugfs"
    assert _reviewed_range_source("min: 48 max: 240", 48, 144) == "config-reviewed"
    assert _connector_profile(
        [("DP-1", "connected", "enabled")], "DP-1") == (1, 1, True, True)
    assert _connector_profile(
        [("DP-1", "connected", "enabled"),
         ("DP-2", "connected", "enabled")], "DP-1") == (2, 2, True, True)
    self_test = run("self-test")
    assert self_test.returncode == 0 and "self-test: ok" in self_test.stdout
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        config, fixture = make_fixture(root)
        duplicate_vt = CONFIG_TEXT.replace(
            'alternate_tty = "/dev/tty1"', 'alternate_tty = "/dev/tty2"')
        config.write_text(duplicate_vt, encoding="utf-8")
        try:
            parse_config(config)
        except ConfigError:
            pass
        else:
            raise AssertionError("duplicate active and alternate VTs were accepted")
        config.write_text(CONFIG_TEXT, encoding="utf-8")
        unconfirmed = run("milestone14-vrr-test", "--config", str(config),
                          "--required-base", REQUIRED_BASE,
                          "--tested-commit", TESTED_COMMIT,
                          "--artifact-dir", str(root / "unconfirmed"), "--dry-run",
                          "--fixture-dir", str(fixture))
        assert unconfirmed.returncode == 2 and "literal --yes" in unconfirmed.stderr
        direct_artifacts = root / "direct-live"
        direct = run("milestone14-vrr-test", "--config", str(config),
                     "--required-base", REQUIRED_BASE,
                     "--tested-commit", TESTED_COMMIT,
                     "--artifact-dir", str(direct_artifacts), "--yes")
        assert direct.returncode == 1
        assert "requires systemd-run --scope" in direct.stderr
        assert not direct_artifacts.exists()
        doctor_artifacts = root / "doctor-artifacts"
        checked = run("doctor", "--config", str(config),
                      "--required-base", REQUIRED_BASE,
                      "--tested-commit", TESTED_COMMIT,
                      "--fixture-dir", str(fixture),
                      "--artifact-dir", str(doctor_artifacts))
        assert checked.returncode == 0, checked.stderr
        report = json.loads((doctor_artifacts / "milestone14-hardware-doctor.json").read_text())
        assert report["passed"] is True
        identity_rejected = run("doctor", "--config", str(config),
                                "--required-base", REQUIRED_BASE,
                                "--tested-commit", "c" * 40,
                                "--fixture-dir", str(fixture))
        assert identity_rejected.returncode == 1
        assert "does not match" in identity_rejected.stderr

        unavailable = json.loads((fixture / "doctor.json").read_text())
        unavailable["selected_mode_available"] = False
        write_json(fixture / "doctor.json", unavailable)
        rejected = run("doctor", "--config", str(config),
                       "--required-base", REQUIRED_BASE,
                       "--tested-commit", TESTED_COMMIT,
                       "--fixture-dir", str(fixture))
        assert rejected.returncode == 1 and "selected mode available" in rejected.stdout
        unavailable["selected_mode_available"] = True
        write_json(fixture / "doctor.json", unavailable)

        unsafe_vt = dict(unavailable, active_tty="/dev/tty3",
                         spare_tty=False, alternate_tty_safe=False)
        write_json(fixture / "doctor.json", unsafe_vt)
        wrong_vt = run("doctor", "--config", str(config),
                       "--required-base", REQUIRED_BASE,
                       "--tested-commit", TESTED_COMMIT,
                       "--fixture-dir", str(fixture))
        assert wrong_vt.returncode == 1
        assert "exact active_tty" in wrong_vt.stdout

        unsafe_profile = dict(unavailable, active_connector_count=2,
                              connected_connector_count=2,
                              single_connected_active_connector=False)
        write_json(fixture / "doctor.json", unsafe_profile)
        multiple = run("doctor", "--config", str(config),
                       "--required-base", REQUIRED_BASE,
                       "--tested-commit", TESTED_COMMIT,
                       "--fixture-dir", str(fixture))
        assert multiple.returncode == 1
        assert "exactly one connected connector" in multiple.stdout
        write_json(fixture / "doctor.json", unavailable)

        artifacts = root / "artifacts"
        completed = run("milestone14-vrr-test", "--config", str(config), "--yes",
                        "--required-base", REQUIRED_BASE,
                        "--tested-commit", TESTED_COMMIT,
                        "--dry-run", "--fixture-dir", str(fixture),
                        "--artifact-dir", str(artifacts))
        assert completed.returncode == 0, completed.stderr
        off = json.loads((artifacts / "milestone14-vrr-off-summary.json").read_text())
        on = json.loads((artifacts / "milestone14-vrr-on-summary.json").read_text())
        summary = json.loads((artifacts / "milestone14-hardware-summary.json").read_text())
        assert off["passed"] and off["pass_percentage"] < 25
        assert on["passed"] and on["sample_count"] == 130 and on["pass_percentage"] >= 75
        assert summary["passed"] and summary["run_step_count"] == 29
        assert summary["required_base_commit"] == REQUIRED_BASE
        assert summary["tested_commit"] == TESTED_COMMIT
        assert_archive(artifacts)

        provenance_path = fixture / "milestone14-build-provenance.json"
        provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
        write_json(provenance_path, dict(provenance, source_commit="c" * 40))
        rejected_artifacts = root / "provenance-rejected"
        rejected = run(
            "milestone14-vrr-test", "--config", str(config), "--yes",
            "--required-base", REQUIRED_BASE,
            "--tested-commit", TESTED_COMMIT,
            "--dry-run", "--fixture-dir", str(fixture),
            "--artifact-dir", str(rejected_artifacts),
        )
        assert rejected.returncode == 1
        assert "build provenance" in rejected.stderr
        write_json(provenance_path, provenance)

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        config, fixture = make_fixture(root, restored=False)
        artifacts = root / "failed-artifacts"
        failed = run("milestone14-vrr-test", "--config", str(config), "--yes",
                     "--required-base", REQUIRED_BASE,
                     "--tested-commit", TESTED_COMMIT,
                     "--dry-run", "--fixture-dir", str(fixture),
                     "--artifact-dir", str(artifacts))
        assert failed.returncode == 1 and "restoration" in failed.stderr
        summary = json.loads((artifacts / "milestone14-hardware-summary.json").read_text())
        assert not summary["passed"] and summary["failure_stage"] == "restoration"
        assert summary["restoration_attempted"] is True
    print("gw-hw parser/doctor/dry-run test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
