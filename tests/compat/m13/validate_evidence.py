#!/usr/bin/env python3
"""Build and validate the Milestone 13 VM summary from collected facts."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import tarfile
import tempfile

import validate_frame_sets


GWIPC_OUTPUT_CONFIGURATION_ACCEPTED = 1
REQUIRED_BASE = "d3440d3b8df1533410a9a2c4be46f2eea0cfb88d"
REQUIRED_RESULTS = (
    "historical_default strict_software strict_gles sanitizer component_builds "
    "source_layout api_consumers m1_m12_regressions output_inventory "
    "stable_id_replay logical_physical_geometry integer_scaling "
    "output_enable_disable pointer_output_crossing sdl_display_discovery "
    "fullscreen_outputs "
    "fractional_scaling transforms surface_membership primary_transition "
    "legacy_fallback scaled_pixmap gw_scale_events multi_output_randr "
    "gwinfo_text gwinfo_json gwout_commit stale_rejection busy_rejection "
    "headless_frame_hashes aggregate_hash renderer_comparison gwm_replay "
    "compositor_replay drm_scale_transform screenshot_equality vt_replay "
    "restoration service_results socket_cleanup archive_validation "
    "journal_evidence"
).split()
IDENTITY = {
    "api_version": "0.8.0", "soversion": "0", "wire_version": "1.0",
    "api_consumer_versions": "0.1-0.8", "source_layout_allowlist": "empty",
    "output_descriptor_count": "2", "stable_output_ids": "true",
    "headless_output_names": "LEFT,RIGHT", "primary_output": "LEFT",
    "root_logical_geometry": "1280x480", "control_socket_mode": "0600",
    "x_servers_absent": "true", "display_manager_absent": "true",
}
RECORDED = (
    "compiler_c", "compiler_cxx", "meson_version", "ninja_version",
    "systemd_version", "layout_generation", "output_ids",
    "headless_aggregate_hash", "drm_connector", "drm_mode",
)
IMPORTANT_ARTIFACTS = (
    "milestone13-runtime-test.log milestone13-meson-test.log "
    "milestone13-sdl-build.log milestone13-source-layout.log "
    "milestone13-output-inventory.json milestone13-layout-before.json "
    "milestone13-layout-after.json milestone13-gwinfo-outputs.json "
    "milestone13-gwinfo-windows.json milestone13-gwout.log "
    "milestone13-gwout-result.json milestone13-randr.log "
    "milestone13-randr-little.json "
    "milestone13-randr-big.json milestone13-gw-scale-little.json "
    "milestone13-gw-scale-big.json milestone13-gw-scale.log "
    "milestone13-scale-client.json "
    "milestone13-frame-sets.jsonl milestone13-pointer-crossing.json "
    "milestone13-sdl-displays.json milestone13-fullscreen-outputs.json "
    "milestone13-renderer-software.jsonl "
    "milestone13-renderer-gles.jsonl milestone13-renderer-drm.jsonl "
    "milestone13-renderer-fractional-diff.json "
    "milestone13-drm-report.jsonl milestone13-drm-representation.json "
    "milestone13-kms-before.json "
    "milestone13-kms-after.json milestone13-vt-before.json "
    "milestone13-vt-after.json milestone13-restart.json "
    "milestone13-restoration.json milestone13-getty-state.json "
    "milestone13-logind-state.json milestone13-glasswyrmd-journal.log "
    "milestone13-gwm-journal.log milestone13-gwcomp-journal.log "
    "milestone13-session-journal.log milestone13-facts.env "
    "milestone13-headless-outputs.tar milestone13-drm-canonical.ppm "
    "milestone13-drm-screen.ppm "
    "milestone13-output-scaling-evidence.tar"
).split()
EVIDENCE_MEMBERS = {
    "milestone13-output-inventory.json", "milestone13-layout-before.json",
    "milestone13-layout-after.json", "milestone13-randr-little.json",
    "milestone13-randr-big.json", "milestone13-gw-scale-little.json",
    "milestone13-gw-scale-big.json", "milestone13-gwinfo-outputs.json",
    "milestone13-gwinfo-windows.json", "milestone13-gwout-result.json",
    "milestone13-scale-client.json",
    "milestone13-frame-sets.jsonl", "milestone13-pointer-crossing.json",
    "milestone13-sdl-displays.json", "milestone13-fullscreen-outputs.json",
    "milestone13-renderer-fractional-diff.json",
    "milestone13-drm-report.jsonl", "milestone13-restoration.json",
    "milestone13-getty-state.json", "milestone13-logind-state.json",
    "milestone13-legacy-left.ppm", "milestone13-legacy-right.ppm",
    "milestone13-legacy-spanning-left.ppm",
    "milestone13-legacy-spanning-right.ppm",
    "milestone13-aware-left.ppm", "milestone13-aware-right.ppm",
    "milestone13-rotate90.ppm", "milestone13-flipped.ppm",
    "milestone13-renderer-software.jsonl", "milestone13-renderer-gles.jsonl",
    "milestone13-renderer-drm.jsonl", "milestone13-drm-representation.json",
    "milestone13-restart.json", "milestone13-kms-before.json",
    "milestone13-kms-after.json", "milestone13-vt-before.json",
    "milestone13-vt-after.json", "milestone13-scene.jsonl",
}


def load_facts(path: pathlib.Path) -> dict[str, str]:
    facts: dict[str, str] = {}
    if path.is_file():
        for line in path.read_text(errors="replace").splitlines():
            key, separator, value = line.partition("=")
            if separator:
                facts[key] = value
    return facts


def load_object(path: pathlib.Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} is not a JSON object")
    return value


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        raise ValueError(f"{path.name} is not an 8-bit binary PPM")
    width, height = (int(value) for value in match.groups())
    pixels = data[match.end():]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError(f"{path.name} has inconsistent PPM dimensions")
    return width, height, pixels


def extract_flat_archive(path: pathlib.Path, destination: pathlib.Path) -> set[str]:
    names: set[str] = set()
    with tarfile.open(path, "r:") as archive:
        for member in archive.getmembers():
            stripped = member.name.removeprefix("./")
            if member.isdir() and stripped in {"", "."}:
                continue
            name = pathlib.PurePosixPath(stripped)
            if (name.is_absolute() or ".." in name.parts or len(name.parts) != 1
                    or not member.isfile() or name.name in names):
                raise ValueError(f"{path.name} has unsafe member {member.name!r}")
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f"{path.name} member {member.name!r} is unreadable")
            (destination / name.name).write_bytes(source.read())
            names.add(name.name)
    return names


def validate_checksums(root: pathlib.Path, names: set[str]) -> None:
    sums = root / "SHA256SUMS"
    if not sums.is_file():
        raise ValueError("evidence archive has no SHA256SUMS")
    listed: set[str] = set()
    for number, line in enumerate(sums.read_text(encoding="ascii").splitlines(), 1):
        expected, separator, name = line.partition("  ")
        if (not separator or not re.fullmatch(r"[0-9a-f]{64}", expected)
                or pathlib.PurePath(name).name != name or name in listed
                or name == "SHA256SUMS" or name not in names):
            raise ValueError(f"evidence SHA256SUMS line {number} is invalid")
        actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"evidence checksum differs for {name}")
        listed.add(name)
    if listed != names - {"SHA256SUMS"}:
        raise ValueError("evidence checksum inventory is incomplete")


def validate_protocol_result(path: pathlib.Path, byte_order: str) -> None:
    value = load_object(path)
    if (value.get("byte_order") != byte_order or value.get("passed") is not True
            or value.get("errors") not in (None, [])):
        raise ValueError(f"{path.name} is not passing {byte_order}-endian evidence")


def validate_gwout_result(path: pathlib.Path) -> None:
    value = load_object(path)
    if (value.get("result") != GWIPC_OUTPUT_CONFIGURATION_ACCEPTED
            or not isinstance(value.get("applied_generation"), int)
            or value["applied_generation"] <= 0):
        raise ValueError(f"{path.name} is not an accepted commit")


def validate_restarted_renderer_report(path: pathlib.Path) -> None:
    records = [json.loads(line) for line in
               path.read_text(encoding="utf-8").splitlines() if line.strip()]
    selections = [index for index, record in enumerate(records)
                  if record.get("record") == "selection"]
    if (len(selections) != 2
            or any(records[index].get("selected") != "software"
                   for index in selections)
            or any(record.get("record") == "fatal" for record in records)):
        raise ValueError(f"{path.name} does not contain two software lifetimes")
    boundaries = selections[1:] + [len(records)]
    for start, end in zip(selections, boundaries):
        if not any(record.get("record") == "output-frame"
                   and record.get("selected") == "software"
                   and record.get("disposition") == "complete"
                   for record in records[start + 1:end]):
            raise ValueError(
                f"{path.name} has no complete frame after a restart boundary")


def validate_artifacts(root: pathlib.Path, facts: dict[str, str]) -> list[str]:
    errors: list[str] = []
    try:
        missing = [name for name in IMPORTANT_ARTIFACTS
                   if not (root / name).is_file() or (root / name).stat().st_size == 0]
        if missing:
            raise ValueError("important artifacts are missing or empty: "
                             + ", ".join(missing))

        inventory = load_object(root / "milestone13-output-inventory.json")
        outputs = inventory.get("outputs")
        if (not isinstance(outputs, list) or len(outputs) != 2
                or [item.get("name") for item in outputs
                    if isinstance(item, dict)] != ["LEFT", "RIGHT"]):
            raise ValueError("output inventory does not contain LEFT and RIGHT")
        ids = [item.get("id") for item in outputs]
        if ",".join(ids) != facts.get("output_ids"):
            raise ValueError("output inventory IDs differ from recorded facts")

        before = load_object(root / "milestone13-layout-before.json")
        after = load_object(root / "milestone13-layout-after.json")
        if ((before.get("root_width"), before.get("root_height")) != (1440, 600)
                or (after.get("root_width"), after.get("root_height")) != (1280, 480)
                or after.get("layout_generation") != before.get("layout_generation") + 1):
            raise ValueError("canonical layout artifacts differ")
        for protocol in ("randr", "gw-scale"):
            for order in ("little", "big"):
                validate_protocol_result(
                    root / f"milestone13-{protocol}-{order}.json", order)

        validate_gwout_result(root / "milestone13-gwout-result.json")
        validate_restarted_renderer_report(
            root / "milestone13-renderer-software.jsonl")

        scale_client = load_object(root / "milestone13-scale-client.json")
        if (scale_client.get("schema") != "glasswyrm.m13-scale-client.v1"
                or scale_client.get("logical_geometry") != {"width": 320, "height": 240}
                or scale_client.get("buffer_geometry") != {"width": 640, "height": 480}
                or scale_client.get("initial", {}).get("memberships")
                == scale_client.get("moved", {}).get("memberships")
                or scale_client.get("present_serial") != 1
                or scale_client.get("reset_scale") != 1):
            raise ValueError("scale-client artifact does not prove its M13 contract")
        pointer = load_object(root / "milestone13-pointer-crossing.json")
        if pointer.get("passed") is not True or len(pointer.get("cursor_output_ids", [])) < 2:
            raise ValueError("pointer-crossing artifact is incomplete")
        sdl = load_object(root / "milestone13-sdl-displays.json")
        if (sdl.get("passed") is not True or sdl.get("sdl_version") != "2.32.10"
                or sdl.get("display_count") != 2 or len(sdl.get("displays", [])) != 2):
            raise ValueError("SDL artifact does not prove two displays")
        fullscreen = load_object(root / "milestone13-fullscreen-outputs.json")
        left = fullscreen.get("left", {})
        right = fullscreen.get("right", {})
        left_restored = fullscreen.get("left_restored", {})
        right_restored = fullscreen.get("right_restored", {})
        if (fullscreen.get("schema") != 1 or fullscreen.get("passed") is not True
                or left.get("fullscreen") is not True
                or right.get("fullscreen") is not True
                or left_restored.get("fullscreen") is not False
                or right_restored.get("fullscreen") is not False
                or (left.get("logical_x"), left.get("logical_y"),
                    left.get("logical_width"), left.get("logical_height"))
                != (0, 0, 640, 480)
                or (right.get("logical_x"), right.get("logical_y"),
                    right.get("logical_width"), right.get("logical_height"))
                != (640, 0, 640, 480)
                or left.get("primary_output_id") == right.get("primary_output_id")):
            raise ValueError("fullscreen artifact does not prove both outputs")
        difference = load_object(root / "milestone13-renderer-fractional-diff.json")
        maximum = difference.get("maximum_channel_difference")
        if (difference.get("passed") is not True or not isinstance(maximum, int)
                or not 0 <= maximum <= 1 or len(difference.get("outputs", [])) != 2):
            raise ValueError("renderer comparison artifact exceeds its bound")
        if load_object(root / "milestone13-restoration.json").get("passed") is not True:
            raise ValueError("restoration artifact did not pass")
        for name in ("milestone13-getty-state.json", "milestone13-logind-state.json"):
            if load_object(root / name).get("restored") is not True:
                raise ValueError(f"{name} does not prove restoration")

        canonical = read_ppm(root / "milestone13-drm-canonical.ppm")
        screenshot = read_ppm(root / "milestone13-drm-screen.ppm")
        if canonical != screenshot or not any(canonical[2]):
            raise ValueError("DRM canonical and screenshot evidence differs or is blank")
        representation = load_object(root / "milestone13-drm-representation.json")
        canonical_sha256 = hashlib.sha256(
            (root / "milestone13-drm-canonical.ppm").read_bytes()).hexdigest()
        releases = representation.get("resource_release")
        if (representation.get("schema") != 1
                or representation.get("passed") is not True
                or representation.get("scale") != [4, 3]
                or representation.get("transform") != "rotate-180"
                or representation.get("canonical_sha256") != canonical_sha256
                or representation.get("post_vt_sha256") != canonical_sha256
                or not isinstance(representation.get("vt_release_count"), int)
                or representation["vt_release_count"] <= 0
                or not isinstance(representation.get("vt_acquire_count"), int)
                or representation["vt_acquire_count"] <= 0
                or not isinstance(releases, dict)
                or any(releases.get(key) is not True for key in (
                    "main_pid_zero", "framebuffer_cleanup", "master_drop",
                    "texture_cache_zero", "event_fd_closed", "no_fatal"))):
            raise ValueError("DRM representation artifact does not prove recovery")

        with tempfile.TemporaryDirectory(prefix="m13-headless-") as temporary:
            extracted = pathlib.Path(temporary)
            names = extract_flat_archive(
                root / "milestone13-headless-outputs.tar", extracted)
            if "frame-sets.jsonl" not in names:
                raise ValueError("headless archive has no frame-set manifest")
            aggregate = validate_frame_sets.validate_manifest(
                extracted / "frame-sets.jsonl", extracted)
            if aggregate != facts.get("headless_aggregate_hash"):
                raise ValueError("headless aggregate differs from recorded facts")
            if ((root / "milestone13-frame-sets.jsonl").read_bytes()
                    != (extracted / "frame-sets.jsonl").read_bytes()):
                raise ValueError("collected frame-set manifest differs from headless archive")

        with tempfile.TemporaryDirectory(prefix="m13-evidence-") as temporary:
            extracted = pathlib.Path(temporary)
            names = extract_flat_archive(
                root / "milestone13-output-scaling-evidence.tar", extracted)
            validate_checksums(extracted, names)
            missing = EVIDENCE_MEMBERS - names
            if missing:
                raise ValueError("evidence archive lacks: " + ", ".join(sorted(missing)))
            validate_gwout_result(extracted / "milestone13-gwout-result.json")
            for name in ("milestone13-gwinfo-outputs.json",
                         "milestone13-gwinfo-windows.json",
                         "milestone13-gwout-result.json"):
                if (root / name).read_bytes() != (extracted / name).read_bytes():
                    raise ValueError(
                        f"evidence archive {name} differs from collected artifact")
            validate_frame_sets.validate_manifest(
                extracted / "milestone13-frame-sets.jsonl", extracted)
            if ((root / "milestone13-frame-sets.jsonl").read_bytes()
                    != (extracted / "milestone13-frame-sets.jsonl").read_bytes()):
                raise ValueError("evidence archive frame-set manifest differs")
    except (OSError, UnicodeError, json.JSONDecodeError, tarfile.TarError,
            ValueError, TypeError) as error:
        errors.append(str(error))
    return errors


def build_summary(facts: dict[str, str], requested: bool, failure: str,
                  tested: str, artifact_errors: list[str] | None = None) -> dict:
    errors = [f"{key} must be passed" for key in REQUIRED_RESULTS
              if facts.get(key) != "passed"]
    if facts.get("clang") not in {"passed", "unavailable"}:
        errors.append("clang must be passed or unavailable")
    errors.extend(f"{key} must be {expected}"
                  for key, expected in IDENTITY.items()
                  if facts.get(key) != expected)
    errors.extend(f"{key} must be recorded" for key in RECORDED
                  if facts.get(key) in {None, "", "unknown", "unavailable"})
    if facts.get("required_base_commit") != REQUIRED_BASE:
        errors.append(f"required_base_commit must be {REQUIRED_BASE}")
    if facts.get("tested_commit") != tested:
        errors.append(f"tested_commit must be {tested}")
    if not re.fullmatch(r"[0-9a-f]{40}", tested):
        errors.append("tested_commit must be a lowercase 40-character hash")
    if not facts.get("layout_generation", "").isdigit():
        errors.append("layout_generation must be an integer")
    if not re.fullmatch(r"[0-9a-f]{16},[0-9a-f]{16}",
                        facts.get("output_ids", "")):
        errors.append("output_ids must contain two stable 64-bit identifiers")
    if not re.fullmatch(r"[0-9a-f]{16}",
                        facts.get("headless_aggregate_hash", "")):
        errors.append("headless_aggregate_hash must be a 64-bit hash")
    if facts.get("scenario_exit") != "0":
        errors.append("scenario_exit must be 0")
    errors.extend(artifact_errors or [])
    return {
        "required_base_commit": REQUIRED_BASE,
        "tested_commit": tested,
        "facts": facts,
        "results": {key: facts.get(key, "unknown") for key in REQUIRED_RESULTS},
        "passed": requested and not errors,
        "failure_stage": failure or facts.get("failure_stage", ""),
        "evidence_errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--facts", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--tested-commit", required=True)
    parser.add_argument("--failure-stage", default="")
    parser.add_argument("--artifact-dir", type=pathlib.Path)
    parser.add_argument("--require-pass", action="store_true")
    arguments = parser.parse_args()
    facts = load_facts(arguments.facts)
    artifact_errors: list[str] = []
    if arguments.artifact_dir is not None:
        artifact_errors = validate_artifacts(arguments.artifact_dir, facts)
    elif arguments.require_pass:
        artifact_errors = ["artifact directory is required for a passing summary"]
    summary = build_summary(facts,
                            arguments.require_pass,
                            arguments.failure_stage,
                            arguments.tested_commit,
                            artifact_errors)
    arguments.output.write_text(json.dumps(summary, indent=2) + "\n")
    if arguments.require_pass and summary["evidence_errors"]:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
