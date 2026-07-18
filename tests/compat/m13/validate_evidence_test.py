#!/usr/bin/env python3
"""Unit coverage for the Milestone 13 evidence summary validator."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile


REQUIRED = (
    "historical_default strict_software strict_gles sanitizer component_builds "
    "source_layout api_consumers m1_m12_regressions output_inventory "
    "stable_id_replay logical_physical_geometry integer_scaling "
    "output_enable_disable pointer_output_crossing sdl_display_discovery "
    "fractional_scaling transforms surface_membership primary_transition "
    "legacy_fallback scaled_pixmap gw_scale_events multi_output_randr "
    "gwinfo_text gwinfo_json gwout_commit stale_rejection busy_rejection "
    "headless_frame_hashes aggregate_hash renderer_comparison gwm_replay "
    "compositor_replay drm_scale_transform screenshot_equality vt_replay "
    "restoration service_results socket_cleanup archive_validation "
    "journal_evidence"
).split()
COMMIT = "1" * 40


def load_validator(path: pathlib.Path):
    sys.path.insert(0, str(path.parent))
    specification = importlib.util.spec_from_file_location(
        "m13_validate_evidence_under_test", path)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def write_facts(path: pathlib.Path, failed: str = "") -> None:
    facts = {
        "required_base_commit": "d3440d3b8df1533410a9a2c4be46f2eea0cfb88d",
        "tested_commit": COMMIT, "failure_stage": "", "scenario_exit": "0",
        "api_version": "0.8.0", "soversion": "0", "wire_version": "1.0",
        "api_consumer_versions": "0.1-0.8", "source_layout_allowlist": "empty",
        "output_descriptor_count": "2", "stable_output_ids": "true",
        "headless_output_names": "LEFT,RIGHT", "primary_output": "LEFT",
        "root_logical_geometry": "1280x480", "control_socket_mode": "0600",
        "x_servers_absent": "true", "display_manager_absent": "true",
        "compiler_c": "gcc test", "compiler_cxx": "g++ test",
        "meson_version": "1.7.0", "ninja_version": "1.12.0",
        "systemd_version": "systemd test", "layout_generation": "4",
        "output_ids": "0000000000000001,0000000000000002",
        "headless_aggregate_hash": "0123456789abcdef",
        "drm_connector": "Virtual-1", "drm_mode": "1024x768",
        "clang": "unavailable",
    }
    facts.update({name: "passed" for name in REQUIRED})
    if failed:
        facts[failed] = "failed"
    path.write_text("".join(f"{key}={value}\n" for key, value in facts.items()))


def ppm() -> bytes:
    return b"P6\n2 2\n255\n" + bytes((1, 2, 3, 4, 5, 6,
                                        7, 8, 9, 10, 11, 12))


def write_artifacts(root: pathlib.Path, validator) -> str:
    for name in validator.IMPORTANT_ARTIFACTS:
        (root / name).write_bytes(b"evidence\n")
    objects = {
        "milestone13-output-inventory.json": {"outputs": [
            {"id": "0000000000000001", "name": "LEFT"},
            {"id": "0000000000000002", "name": "RIGHT"}]},
        "milestone13-layout-before.json": {
            "layout_generation": 3, "root_width": 1440, "root_height": 600},
        "milestone13-layout-after.json": {
            "layout_generation": 4, "root_width": 1280, "root_height": 480},
        "milestone13-scale-client.json": {
            "schema": "glasswyrm.m13-scale-client.v1",
            "logical_geometry": {"width": 320, "height": 240},
            "buffer_geometry": {"width": 640, "height": 480},
            "initial": {"memberships": [1]}, "moved": {"memberships": [2]},
            "present_serial": 1, "reset_scale": 1},
        "milestone13-pointer-crossing.json": {
            "passed": True, "cursor_output_ids": [1, 2]},
        "milestone13-sdl-displays.json": {
            "passed": True, "sdl_version": "2.32.10", "display_count": 2,
            "displays": [{}, {}]},
        "milestone13-renderer-fractional-diff.json": {
            "passed": True, "maximum_channel_difference": 1,
            "outputs": [{}, {}]},
        "milestone13-restoration.json": {"passed": True},
        "milestone13-getty-state.json": {"restored": True},
        "milestone13-logind-state.json": {"restored": True},
    }
    for protocol in ("randr", "gw-scale"):
        for order in ("little", "big"):
            objects[f"milestone13-{protocol}-{order}.json"] = {
                "byte_order": order, "passed": True, "errors": []}
    for name, value in objects.items():
        (root / name).write_text(json.dumps(value) + "\n")

    pixels = ppm()
    (root / "milestone13-drm-canonical.ppm").write_bytes(pixels)
    (root / "milestone13-drm-screen.ppm").write_bytes(pixels)
    canonical_sha256 = hashlib.sha256(pixels).hexdigest()
    (root / "milestone13-drm-representation.json").write_text(json.dumps({
        "schema": 1, "passed": True, "scale": [4, 3],
        "transform": "rotate-180", "canonical_sha256": canonical_sha256,
        "post_vt_sha256": canonical_sha256, "vt_release_count": 1,
        "vt_acquire_count": 1, "resource_release": {
            "main_pid_zero": True, "framebuffer_cleanup": True,
            "master_drop": True, "texture_cache_zero": True,
            "event_fd_closed": True, "no_fatal": True}}) + "\n")
    frame = root / "frame.ppm"
    frame.write_bytes(pixels)
    visible = validator.validate_frame_sets.fnv1a64(pixels.split(b"255\n", 1)[1])
    record = {
        "schema_version": 13, "transaction_ordinal": 1,
        "commit_id": 1, "generation": 1, "layout_generation": 4,
        "primary_output_id": "0000000000000001", "output_count": 1,
        "outputs": [{
            "output_id": "0000000000000001", "file": "frame.ppm",
            "fnv1a64": f"{visible:016x}",
            "physical": {"width": 2, "height": 2},
            "logical": {"x": 0, "y": 0, "width": 2, "height": 2},
            "scale": {"numerator": 1, "denominator": 1},
            "transform": "normal", "damage": []}],
    }
    record["aggregate_hash"] = (
        f"{validator.validate_frame_sets.aggregate_hash(record):016x}")
    manifest = json.dumps(record) + "\n"
    (root / "milestone13-frame-sets.jsonl").write_text(manifest)
    with tarfile.open(root / "milestone13-headless-outputs.tar", "w") as archive:
        archive.add(frame, arcname="frame.ppm")
        archive.add(root / "milestone13-frame-sets.jsonl",
                    arcname="frame-sets.jsonl")

    evidence = root / "evidence"
    evidence.mkdir()
    for name in validator.EVIDENCE_MEMBERS:
        source = root / name
        if source.is_file():
            (evidence / name).write_bytes(source.read_bytes())
        elif name.endswith(".ppm"):
            (evidence / name).write_bytes(pixels)
        else:
            (evidence / name).write_text("{}\n")
    (evidence / "frame.ppm").write_bytes(pixels)
    (evidence / "milestone13-frame-sets.jsonl").write_text(manifest)
    names = sorted(path.name for path in evidence.iterdir())
    (evidence / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256((evidence / name).read_bytes()).hexdigest()}  {name}\n"
        for name in names))
    with tarfile.open(root / "milestone13-output-scaling-evidence.tar", "w") as archive:
        for path in evidence.iterdir():
            archive.add(path, arcname=path.name)
    return record["aggregate_hash"]


with tempfile.TemporaryDirectory() as name:
    root = pathlib.Path(name)
    validator_path = pathlib.Path(sys.argv[1])
    validator = load_validator(validator_path)
    facts = root / "facts.env"
    summary = root / "summary.json"
    write_facts(facts)
    aggregate = write_artifacts(root, validator)
    facts_text = facts.read_text().replace(
        "headless_aggregate_hash=0123456789abcdef",
        f"headless_aggregate_hash={aggregate}")
    facts.write_text(facts_text)
    command = [validator_path, "--facts", str(facts), "--output", str(summary),
               "--tested-commit", COMMIT, "--require-pass"]
    facts_only = subprocess.run(command, text=True, capture_output=True,
                                check=False)
    assert facts_only.returncode == 2
    assert "artifact directory is required" in summary.read_text()

    command.extend(["--artifact-dir", str(root)])
    accepted_run = subprocess.run(command, text=True, capture_output=True,
                                  check=False)
    assert accepted_run.returncode == 0, summary.read_text()
    accepted = json.loads(summary.read_text())
    assert accepted["passed"] is True
    assert accepted["evidence_errors"] == []

    write_facts(facts, "fractional_scaling")
    facts.write_text(facts.read_text().replace(
        "headless_aggregate_hash=0123456789abcdef",
        f"headless_aggregate_hash={aggregate}"))
    result = subprocess.run(command, check=False)
    assert result.returncode == 2
    rejected = json.loads(summary.read_text())
    assert rejected["passed"] is False
    assert "fractional_scaling must be passed" in rejected["evidence_errors"]

    write_facts(facts)
    facts.write_text(facts.read_text().replace(
        "headless_aggregate_hash=0123456789abcdef",
        f"headless_aggregate_hash={aggregate}"))
    (root / "milestone13-drm-screen.ppm").write_bytes(b"P6\n1 1\n255\n\0\0\0")
    result = subprocess.run(command, check=False)
    assert result.returncode == 2
    rejected = json.loads(summary.read_text())
    assert any("DRM canonical" in error for error in rejected["evidence_errors"])
