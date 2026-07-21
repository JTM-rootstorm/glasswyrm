#!/usr/bin/env python3
"""Build and validate the Milestone 14 QXL VM evidence summary."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import tarfile


REQUIRED_BASE = "6864ea631d61636289a21c7d2d6655a17be0c004"
REQUIRED_RESULTS = (
    "historical_default strict_m14 strict_gles sanitizer component_builds "
    "api_consumers source_layout fake_drm_matrix simulated_headless_matrix "
    "raw_little_big gwout_vrr gwinfo_vrr vrr_policy_matrix sdl_vrr_reuse "
    "qxl_unsupported vt_replay "
    "gwm_replay compositor_replay restoration qxl_gwm_replay "
    "qxl_compositor_replay service_results socket_cleanup "
    "archive_validation journal_evidence"
).split()
IDENTITY = {
    "api_version": "0.9.0",
    "soversion": "0",
    "wire_version": "1.0",
    "api_consumer_versions": "0.1-0.9",
    "source_layout_allowlist": "empty",
    "vm_profile": "qxl-unsupported",
    "snapshot_name": "base",
    "headless_outputs": "LEFT,RIGHT",
    "headless_vrr_ranges": "40000-60000,48000-75000",
    "drm_driver": "qxl",
    "x_servers_absent": "true",
    "display_manager_absent": "true",
}
RECORDED = (
    "compiler_c", "compiler_cxx", "meson_version", "ninja_version",
    "systemd_version", "kernel", "libdrm", "drm_connector", "clang",
)
ARTIFACTS = (
    "milestone14-runtime-test.log",
    "milestone14-meson-test.log",
    "milestone14-source-layout.log",
    "milestone14-qxl-capability.json",
    "milestone14-qxl-state.json",
    "milestone14-qxl-drm-report.jsonl",
    "milestone14-qxl-vrr-report.jsonl",
    "milestone14-qxl-kms-before.json",
    "milestone14-qxl-kms-after.json",
    "milestone14-qxl-vt-before.json",
    "milestone14-qxl-vt-after.json",
    "milestone14-qxl-post-vt.json",
    "milestone14-qxl-post-gwm.json",
    "milestone14-qxl-post-gwcomp.json",
    "milestone14-qxl-restart.json",
    "milestone14-headless-report.jsonl",
    "milestone14-gw-vrr.log",
    "milestone14-gwout.log",
    "milestone14-gwinfo.json",
    "milestone14-policy-matrix.json",
    "milestone14-sdl-vrr.json",
    "milestone14-sdl-probe.json",
    "milestone14-client-build.log",
    "milestone14-restart.json",
    "milestone14-restoration.json",
    "milestone14-service-results.json",
    "milestone14-cleanup.json",
    "milestone14-cleanup.log",
    "milestone14-glasswyrmd-journal.log",
    "milestone14-gwm-journal.log",
    "milestone14-gwcomp-journal.log",
    "milestone14-facts.env",
    "milestone14-vm-vrr-evidence.tar",
)
ARCHIVE_MEMBERS = {
    "milestone14-qxl-capability.json",
    "milestone14-qxl-state.json",
    "milestone14-qxl-drm-report.jsonl",
    "milestone14-qxl-vrr-report.jsonl",
    "milestone14-qxl-kms-before.json",
    "milestone14-qxl-kms-after.json",
    "milestone14-qxl-vt-before.json",
    "milestone14-qxl-vt-after.json",
    "milestone14-qxl-post-vt.json",
    "milestone14-qxl-post-gwm.json",
    "milestone14-qxl-post-gwcomp.json",
    "milestone14-qxl-restart.json",
    "milestone14-headless-report.jsonl",
    "milestone14-gw-vrr.log",
    "milestone14-gwout.log",
    "milestone14-gwinfo.json",
    "milestone14-policy-matrix.json",
    "milestone14-sdl-vrr.json",
    "milestone14-sdl-probe.json",
    "milestone14-restart.json",
    "milestone14-restoration.json",
    "milestone14-service-results.json",
}


def load_facts(path: pathlib.Path) -> dict[str, str]:
    facts: dict[str, str] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"facts line {number} has no equals sign")
        key, value = line.split("=", 1)
        if not key or key in facts:
            raise ValueError(f"facts line {number} has an invalid or duplicate key")
        facts[key] = value
    return facts


def load_object(path: pathlib.Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} is not a JSON object")
    return value


def load_jsonl(path: pathlib.Path) -> list[dict]:
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
               if line.strip()]
    if not records or any(not isinstance(record, dict) for record in records):
        raise ValueError(f"{path.name} has no object records")
    return records


def validate_archive(path: pathlib.Path, root: pathlib.Path) -> None:
    with tarfile.open(path, "r:") as archive:
        members = archive.getmembers()
        if any(member.isdir() or not member.isfile() or member.name.startswith("/")
               or ".." in pathlib.PurePosixPath(member.name).parts for member in members):
            raise ValueError("evidence archive contains an unsafe member")
        payloads = {pathlib.PurePosixPath(member.name).name:
                    archive.extractfile(member).read() for member in members}
    expected = ARCHIVE_MEMBERS | {"SHA256SUMS"}
    if set(payloads) != expected:
        raise ValueError("evidence archive member inventory differs")
    checksums: dict[str, str] = {}
    for line in payloads["SHA256SUMS"].decode("utf-8").splitlines():
        digest, name = line.split(None, 1)
        checksums[name.lstrip(" *")] = digest
    if set(checksums) != ARCHIVE_MEMBERS:
        raise ValueError("evidence checksum inventory differs")
    for name in ARCHIVE_MEMBERS:
        if hashlib.sha256(payloads[name]).hexdigest() != checksums[name]:
            raise ValueError(f"evidence checksum differs for {name}")
        if payloads[name] != (root / name).read_bytes():
            raise ValueError(f"evidence archive differs from collected {name}")


def validate_artifacts(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    try:
        missing = [name for name in ARTIFACTS
                   if not (root / name).is_file() or (root / name).stat().st_size == 0]
        if missing:
            raise ValueError("artifacts missing or empty: " + ", ".join(missing))

        capability = load_object(root / "milestone14-qxl-capability.json")
        if (capability.get("profile") != "qxl-unsupported"
                or capability.get("passed") is not True
                or capability.get("driver") != "qxl"
                or capability.get("connector_property_present") is not False
                or capability.get("connector_property_value") is not False
                or capability.get("crtc_property_present") is not True
                or not isinstance(capability.get("crtc_property_id"), int)
                or capability["crtc_property_id"] <= 0
                or capability.get("controllable") is not False
                or capability.get("atomic_test_on") is not False):
            raise ValueError("QXL capability is not the explicit unsupported profile")

        qxl = load_object(root / "milestone14-qxl-state.json")
        if not isinstance(qxl.get("vrr"), list) or len(qxl["vrr"]) != 1:
            raise ValueError("QXL state does not contain one output")
        state = qxl["vrr"][0]
        reasons = set(state.get("reasons", []))
        if (state.get("simulated") is not False
                or state.get("hardware_capable") is not False
                or state.get("kms_controllable") is not False
                or state.get("policy") != "off"
                or state.get("effective_enabled", False) is not False
                or not reasons.intersection({"output-not-vrr-capable",
                                             "vrr-property-missing"})):
            raise ValueError("QXL state makes an unsupported VRR claim")

        qxl_drm = load_jsonl(root / "milestone14-qxl-drm-report.jsonl")
        if (sum(record.get("record") == "discovery" for record in qxl_drm) < 2
                or sum(record.get("record") == "selection" for record in qxl_drm) < 2
                or not any(record.get("record") == "vt"
                           and record.get("transition") == "release"
                           for record in qxl_drm)
                or not any(record.get("record") == "vt"
                           and record.get("transition") == "acquire"
                           and record.get("master_owned") is True
                           for record in qxl_drm)
                or sum(record.get("record") == "restore" for record in qxl_drm) < 2):
            raise ValueError("raw QXL DRM report does not prove restart and VT replay")
        qxl_vrr = load_jsonl(root / "milestone14-qxl-vrr-report.jsonl")
        if (sum(record.get("record") == "vrr-capability" for record in qxl_vrr) < 2
                or sum(record.get("record") == "vrr-restore" for record in qxl_vrr) < 2
                or any(record.get("controllable") is not False
                       for record in qxl_vrr
                       if record.get("record") == "vrr-capability")):
            raise ValueError("raw QXL VRR report does not prove unsupported replay")
        if (load_object(root / "milestone14-qxl-kms-before.json") !=
                load_object(root / "milestone14-qxl-kms-after.json")):
            raise ValueError("raw QXL KMS state was not restored")
        vt_before = load_object(root / "milestone14-qxl-vt-before.json")
        vt_after = load_object(root / "milestone14-qxl-vt-after.json")
        if (vt_before.get("active", [None])[0] != vt_after.get("active", [None])[0]
                or any(vt_before.get(key) != vt_after.get(key)
                       for key in ("mode", "kd", "keyboard"))):
            raise ValueError("raw QXL VT state was not restored")

        def require_qxl_off(name: str) -> dict:
            payload = load_object(root / name)
            values = payload.get("vrr", [])
            if len(values) != 1:
                raise ValueError(f"{name} does not contain one QXL output")
            value = values[0]
            if (value.get("policy") != "off"
                    or value.get("desired_enabled") is not False
                    or value.get("effective_enabled") is not False
                    or value.get("hardware_capable") is not False
                    or value.get("kms_controllable") is not False):
                raise ValueError(f"{name} does not preserve QXL VRR-off state")
            return value

        for qxl_state in ("milestone14-qxl-post-vt.json",
                          "milestone14-qxl-post-gwm.json",
                          "milestone14-qxl-post-gwcomp.json"):
            require_qxl_off(qxl_state)
        qxl_restart = load_object(root / "milestone14-qxl-restart.json")
        qxl_semantic = qxl_restart.get("semantic_state", {})
        if (qxl_restart.get("passed") is not True
                or qxl_restart.get("gwm_replay") is not True
                or qxl_restart.get("compositor_replay") is not True
                or qxl_semantic.get("policy") != "off"
                or qxl_semantic.get("desired_enabled") is not False
                or qxl_semantic.get("effective_enabled") is not False
                or qxl_semantic.get("hardware_capable") is not False
                or qxl_semantic.get("kms_controllable") is not False):
            raise ValueError("QXL restart evidence does not preserve VRR-off state")

        headless = load_jsonl(root / "milestone14-headless-report.jsonl")
        capabilities = [record for record in headless
                        if record.get("record") == "capability"]
        decisions = [record for record in headless if record.get("record") == "decision"]
        timings = [record for record in headless if record.get("record") == "timing"]
        summaries = [record for record in headless if record.get("record") == "summary"]
        restores = [record for record in headless if record.get("record") == "restore"]
        ranges = {(record.get("minimum_refresh_millihertz"),
                   record.get("maximum_refresh_millihertz")) for record in capabilities}
        if (len(capabilities) < 2 or ranges != {(40000, 60000), (48000, 75000)}
                or any(record.get("backend") != "headless"
                       or record.get("device") != "simulated"
                       or record.get("driver") != "headless"
                       or record.get("connector") != "simulated"
                       or record.get("connector_property_present") is not True
                       or record.get("connector_property_value") != 1
                       or record.get("crtc_property_present") is not True
                       or record.get("atomic_test_off") is not True
                       or record.get("atomic_test_on") is not True
                       or record.get("range_source") != "configured"
                       or record.get("simulated") is not True
                       or record.get("hardware_capable") is not False
                       or record.get("controllable") is not True
                       for record in capabilities)
                or not {1, 2, 3, 4, 5}.issubset(
                    {record.get("policy_mode") for record in decisions})
                or not timings or any(record.get("simulated") is not True
                                      for record in timings)
                or len(summaries) < 2
                or any(record.get("simulated") is not True
                       or record.get("sample_count", 0) <= 0
                       for record in summaries)
                or not restores or any(record.get("simulated") is not True
                                       or record.get("original_value") != 0
                                       or record.get("restored_value") != 0
                                       or record.get("readback_success") is not True
                                       or record.get("kms_status") != "not_applicable"
                                       or record.get("vt_status") != "not_applicable"
                                       or record.get("getty_status") != "not_applicable"
                                       for record in restores)):
            raise ValueError("headless report does not prove the simulated VRR matrix")

        protocol_lines = (root / "milestone14-gw-vrr.log").read_text().splitlines()
        protocol = json.loads(next(line for line in reversed(protocol_lines)
                                   if line.startswith("{")))
        if protocol.get("little") is not True or protocol.get("big") is not True:
            raise ValueError("GW_VRR log does not prove both byte orders")

        gwout = load_jsonl(root / "milestone14-gwout.log")
        accepted = [record for record in gwout
                    if record.get("acknowledgement", {}).get("result") == 1]
        rejected = [record for record in gwout
                    if record.get("profile") == "qxl-unsupported"]
        if (len(accepted) != 10 or len(rejected) != 1
                or rejected[0].get("accepted") is not False
                or rejected[0].get("requested_policy") != "always-eligible"
                or rejected[0].get("exit_status") in (None, 0)
                or rejected[0].get("error") !=
                "gwout: selected output does not provide controllable VRR"):
            raise ValueError(
                "gwout log does not contain ten accepted simulated transitions "
                "and the explicit QXL rejection")
        gwinfo = load_object(root / "milestone14-gwinfo.json")
        if (len(gwinfo.get("vrr", [])) != 2
                or any(record.get("simulated") is not True for record in gwinfo["vrr"])):
            raise ValueError("gwinfo does not expose two simulated VRR outputs")
        policy = load_object(root / "milestone14-policy-matrix.json")
        modes = policy.get("modes", {})
        expected_modes = {
            "off": False,
            "fullscreen": True,
            "focused": True,
            "app-requested": True,
            "always-eligible": True,
        }
        transitions = policy.get("app_requested_transitions", [])
        focused = policy.get("focused_candidates", [])
        borderless = policy.get("borderless", {})
        if (policy.get("passed") is not True or set(modes) != set(expected_modes)
                or any(modes[name].get("effective_enabled") is not enabled
                       or modes[name].get("desired_enabled") is not enabled
                       for name, enabled in expected_modes.items())
                or [(value.get("preference"), value.get("effective_enabled"))
                    for value in transitions] != [
                        ("default", False), ("prefer", True),
                        ("disable", False)]
                or len(focused) != 2 or any(not isinstance(value, int) or value == 0
                                            for value in focused)
                or focused[0] == focused[1]
                or borderless.get("effective_enabled") is not True
                or borderless.get("classified") is not True
                or not isinstance(borderless.get("candidate_window"), int)
                or borderless.get("candidate_window") == 0):
            raise ValueError("policy matrix does not prove M14 live transitions")
        sdl = load_object(root / "milestone14-sdl-vrr.json")
        probe = load_object(root / "milestone14-sdl-probe.json")
        if (sdl.get("passed") is not True or sdl.get("sdl_version") != "2.32.10"
                or sdl.get("fullscreen_desktop_enabled") is not True
                or sdl.get("borderless_windowed_rejected") is not True
                or sdl.get("implicit_app_request") is not False
                or sdl.get("app_requested_effective") is not False
                or probe.get("probe") != "m12_sdl_probe"
                or probe.get("sdl_version") != "2.32.10"
                or probe.get("video_driver") != "x11"
                or probe.get("passed") is not True):
            raise ValueError("pinned SDL evidence does not prove M14 behavior")
        restart = load_object(root / "milestone14-restart.json")
        semantic = restart.get("semantic_state", {})
        if (restart.get("passed") is not True or restart.get("gwm_replay") is not True
                or restart.get("compositor_replay") is not True
                or restart.get("candidate_window") in (None, 0)
                or semantic.get("policy") != "focused"
                or semantic.get("decision") != "enabled"
                or semantic.get("desired_enabled") is not True
                or semantic.get("effective_enabled") is not True
                or not isinstance(semantic.get("reasons"), list)):
            raise ValueError("restart evidence is incomplete")
        restoration = load_object(root / "milestone14-restoration.json")
        checks = restoration.get("checks", {})
        if (restoration.get("passed") is not True
                or set(checks) != {"kms", "vt", "vrr", "getty", "logind"}
                or not all(checks.values())):
            raise ValueError("restoration evidence is incomplete")
        service_results = load_object(root / "milestone14-service-results.json")
        services = service_results.get("services", [])
        if (service_results.get("passed") is not True or len(services) != 10
                or any(service.get("LoadState") != "loaded"
                       or service.get("ActiveState") != "inactive"
                       or service.get("Result") != "success"
                       or service.get("ExecMainCode") != "exited"
                       or service.get("ExecMainStatus") != "0"
                       for service in services)):
            raise ValueError("service result gate is incomplete")
        cleanup = load_object(root / "milestone14-cleanup.json")
        cleanup_checks = cleanup.get("restoration_checks", {})
        if (cleanup.get("failure_stage") != "completed"
                or cleanup.get("original_exit_status") != 0
                or cleanup.get("restoration_attempted") is not True
                or cleanup.get("restoration_ok") is not True
                or cleanup.get("cleanup_failures") != 0
                or cleanup_checks != {name: "passed" for name in
                                      ("kms", "vt", "getty", "logind")}):
            raise ValueError("successful-run cleanup diagnostics are incomplete")
        validate_archive(root / "milestone14-vm-vrr-evidence.tar", root)
    except (OSError, ValueError, KeyError, StopIteration, json.JSONDecodeError,
            tarfile.TarError) as error:
        errors.append(str(error))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--facts", type=pathlib.Path, required=True)
    parser.add_argument("--artifact-dir", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--tested-commit", required=True)
    parser.add_argument("--failure-stage", default="")
    parser.add_argument("--require-pass", action="store_true")
    arguments = parser.parse_args()
    errors: list[str] = []
    facts: dict[str, str] = {}
    try:
        facts = load_facts(arguments.facts)
    except (OSError, ValueError) as error:
        errors.append(str(error))
    if facts:
        if facts.get("required_base_commit") != REQUIRED_BASE:
            errors.append("required_base_commit differs")
        if (not re.fullmatch(r"[0-9a-f]{40}", arguments.tested_commit)
                or facts.get("tested_commit") != arguments.tested_commit):
            errors.append("tested_commit differs or is not a full commit ID")
        for key, expected in IDENTITY.items():
            if facts.get(key) != expected:
                errors.append(f"{key} must be {expected}")
        for key in RECORDED:
            if not facts.get(key):
                errors.append(f"{key} must be recorded")
        if facts.get("scenario_exit") != "0":
            errors.append("scenario_exit must be 0")
        if facts.get("failure_stage") != "completed":
            errors.append("failure_stage must be completed")
        for key in REQUIRED_RESULTS:
            if facts.get(key) != "passed":
                errors.append(f"{key} must be passed")
    if arguments.require_pass:
        if arguments.artifact_dir is None:
            errors.append("artifact directory is required for passing evidence")
        else:
            errors.extend(validate_artifacts(arguments.artifact_dir))
    summary = {
        "schema": "glasswyrm.m14-vm-evidence.v1",
        "required_base_commit": REQUIRED_BASE,
        "tested_commit": arguments.tested_commit,
        "profile": "qxl-unsupported",
        "passed": not errors,
        "failure_stage": arguments.failure_stage or facts.get("failure_stage", ""),
        "results": {key: facts.get(key, "missing") for key in REQUIRED_RESULTS},
        "evidence_errors": errors,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    return 0 if not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
