#!/usr/bin/env python3
"""Unit coverage for the Milestone 14 QXL VM evidence validator."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile


COMMIT = "1" * 40


def load_validator(path: pathlib.Path):
    specification = importlib.util.spec_from_file_location("m14_vm_evidence", path)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def write_facts(path: pathlib.Path, validator, failed: str = "") -> None:
    facts = {
        "required_base_commit": validator.REQUIRED_BASE,
        "tested_commit": COMMIT,
        "failure_stage": "completed",
        "scenario_exit": "0",
        **validator.IDENTITY,
        "compiler_c": "gcc test", "compiler_cxx": "g++ test",
        "meson_version": "1.8.0", "ninja_version": "1.12.0",
        "systemd_version": "systemd test", "kernel": "test-kernel",
        "libdrm": "2.4.test", "drm_connector": "Virtual-1",
        "clang": "unavailable",
    }
    facts.update({name: "passed" for name in validator.REQUIRED_RESULTS})
    if failed:
        facts[failed] = "failed"
    path.write_text("".join(f"{key}={value}\n" for key, value in facts.items()))


def write_json(path: pathlib.Path, value) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n")


def write_artifacts(root: pathlib.Path, validator) -> None:
    for name in validator.ARTIFACTS:
        (root / name).write_text("evidence\n")
    write_json(root / "milestone14-qxl-capability.json", {
        "schema": 1, "profile": "qxl-unsupported", "passed": True,
        "driver": "qxl", "connector_property_present": False,
        "connector_property_value": False,
        "crtc_property_present": True, "crtc_property_id": 24,
        "controllable": False,
        "atomic_test_on": False,
    })
    write_json(root / "milestone14-qxl-state.json", {"vrr": [{
        "simulated": False, "hardware_capable": False, "policy": "off",
        "kms_controllable": False, "effective_enabled": False,
        "reasons": ["output-not-vrr-capable", "vrr-property-missing"],
    }]})
    headless = [
        {"record": "capability", "backend": "headless",
         "device": "simulated", "driver": "headless",
         "connector": "simulated", "connector_property_present": True,
         "connector_property_value": 1, "crtc_property_present": True,
         "atomic_test_off": True, "atomic_test_on": True,
         "range_source": "configured", "simulated": True,
         "hardware_capable": False, "controllable": True,
         "minimum_refresh_millihertz": 40000,
         "maximum_refresh_millihertz": 60000},
        {"record": "capability", "backend": "headless",
         "device": "simulated", "driver": "headless",
         "connector": "simulated", "connector_property_present": True,
         "connector_property_value": 1, "crtc_property_present": True,
         "atomic_test_off": True, "atomic_test_on": True,
         "range_source": "configured", "simulated": True,
         "hardware_capable": False, "controllable": True,
         "minimum_refresh_millihertz": 48000,
         "maximum_refresh_millihertz": 75000},
        *({"record": "decision", "simulated": True, "policy_mode": mode}
          for mode in range(1, 6)),
        {"record": "timing", "simulated": True},
        {"record": "summary", "simulated": True, "sample_count": 2},
        {"record": "summary", "simulated": True, "sample_count": 2},
        {"record": "restore", "simulated": True, "original_value": 0,
         "restored_value": 0, "readback_success": True,
         "kms_status": "not_applicable", "vt_status": "not_applicable",
         "getty_status": "not_applicable"},
    ]
    (root / "milestone14-headless-report.jsonl").write_text(
        "".join(json.dumps(item) + "\n" for item in headless))
    (root / "milestone14-gw-vrr.log").write_text(
        "test output\n" + json.dumps({"little": True, "big": True}) + "\n")
    accepted = [{"acknowledgement": {"result": 1,
                 "applied_generation": index + 1}, "state": {}}
                for index in range(10)]
    accepted.append({"profile": "qxl-unsupported",
                     "requested_policy": "always-eligible", "accepted": False,
                     "exit_status": 1,
                     "error": "gwout: selected output does not provide controllable VRR"})
    (root / "milestone14-gwout.log").write_text(
        "".join(json.dumps(record) + "\n" for record in accepted))
    write_json(root / "milestone14-gwinfo.json", {"vrr": [
        {"simulated": True}, {"simulated": True}]})
    write_json(root / "milestone14-policy-matrix.json", {
        "schema": 1, "passed": True,
        "modes": {
            name: {"desired_enabled": enabled,
                   "effective_enabled": enabled,
                   "candidate_window": index + 20, "reasons": []}
            for index, (name, enabled) in enumerate((
                ("off", False), ("fullscreen", True), ("focused", True),
                ("app-requested", True), ("always-eligible", True)))},
        "app_requested_transitions": [
            {"preference": "default", "effective_enabled": False},
            {"preference": "prefer", "effective_enabled": True},
            {"preference": "disable", "effective_enabled": False}],
        "focused_candidates": [101, 102],
        "borderless": {"effective_enabled": True, "classified": True,
                       "candidate_window": 103},
    })
    write_json(root / "milestone14-sdl-vrr.json", {
        "schema": 1, "passed": True, "sdl_version": "2.32.10",
        "fullscreen_desktop_enabled": True,
        "borderless_windowed_rejected": True,
        "implicit_app_request": False, "app_requested_effective": False,
    })
    write_json(root / "milestone14-sdl-probe.json", {
        "schema": 1, "probe": "m12_sdl_probe", "sdl_version": "2.32.10",
        "video_driver": "x11", "passed": True,
    })
    write_json(root / "milestone14-restart.json", {
        "passed": True, "gwm_replay": True, "compositor_replay": True,
        "candidate_window": 101,
        "semantic_state": {"policy": "focused", "decision": "enabled",
                           "desired_enabled": True, "effective_enabled": True,
                           "reasons": []}})
    write_json(root / "milestone14-restoration.json", {
        "passed": True, "checks": {name: True for name in
        ("kms", "vt", "vrr", "getty", "logind")}})

    evidence = root / "evidence"
    evidence.mkdir()
    for name in validator.ARCHIVE_MEMBERS:
        (evidence / name).write_bytes((root / name).read_bytes())
    checksums = "".join(
        f"{hashlib.sha256((evidence / name).read_bytes()).hexdigest()}  {name}\n"
        for name in sorted(validator.ARCHIVE_MEMBERS))
    (evidence / "SHA256SUMS").write_text(checksums)
    with tarfile.open(root / "milestone14-vm-vrr-evidence.tar", "w") as archive:
        for child in sorted(evidence.iterdir()):
            archive.add(child, arcname=child.name)


def run(command: list[object]) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(item) for item in command], text=True,
                          capture_output=True, check=False)


with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    validator_path = pathlib.Path(sys.argv[1])
    validator = load_validator(validator_path)
    schema = json.loads(
        validator_path.with_name("vm_evidence.schema.json").read_text())
    assert schema["properties"]["schema"]["const"] == \
        "glasswyrm.m14-vm-evidence.v1"
    assert schema["properties"]["required_base_commit"]["const"] == \
        validator.REQUIRED_BASE
    facts = root / "facts.env"
    summary = root / "summary.json"
    write_facts(facts, validator)
    write_artifacts(root, validator)
    command = [validator_path, "--facts", facts, "--artifact-dir", root,
               "--output", summary, "--tested-commit", COMMIT, "--require-pass"]
    accepted = run(command)
    assert accepted.returncode == 0, summary.read_text()
    assert json.loads(summary.read_text())["passed"] is True

    capability = root / "milestone14-qxl-capability.json"
    saved = capability.read_text()
    write_json(capability, {"profile": "qxl-unsupported", "passed": True,
        "driver": "qxl", "connector_property_present": True})
    rejected = run(command)
    assert rejected.returncode == 2
    assert any("unsupported profile" in error
               for error in json.loads(summary.read_text())["evidence_errors"])
    capability.write_text(saved)

    unsupported = json.loads(saved)
    unsupported["crtc_property_present"] = False
    write_json(capability, unsupported)
    rejected = run(command)
    assert rejected.returncode == 2
    assert any("unsupported profile" in error
               for error in json.loads(summary.read_text())["evidence_errors"])
    capability.write_text(saved)

    write_facts(facts, validator, "qxl_unsupported")
    rejected = run(command)
    assert rejected.returncode == 2
    assert "qxl_unsupported must be passed" in json.loads(
        summary.read_text())["evidence_errors"]

    write_facts(facts, validator)
    archive = root / "milestone14-vm-vrr-evidence.tar"
    archive.write_bytes(b"not a tar")
    rejected = run(command)
    assert rejected.returncode == 2
    assert json.loads(summary.read_text())["passed"] is False

print("m14 VM evidence validator: passed")
