#!/usr/bin/env python3
"""Normalize and strictly validate fixed M12 VM runtime evidence."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


def records(path: pathlib.Path) -> list[dict[str, Any]]:
    values: list[dict[str, Any]] = []
    for number, line in enumerate(path.read_text(errors="strict").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{number}: invalid JSON: {error}") from error
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{number}: record must be an object")
        values.append(value)
    if not values:
        raise ValueError(f"{path}: report is empty")
    return values


def nonempty(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def validate_renderer(path: pathlib.Path, expected: str) -> dict[str, Any]:
    values = records(path)
    selections = [item for item in values if item.get("record") == "selection"]
    frames = [item for item in values if item.get("record") == "frame"]
    errors: list[str] = []
    if not selections:
        errors.append("renderer report must contain a selection record")
        selection: dict[str, Any] = {}
    else:
        selection = selections[0]
        if any(item != selection for item in selections[1:]):
            errors.append("renderer restarts changed the selected renderer identity")
    if selection.get("requested") != expected or selection.get("selected") != expected:
        errors.append(f"renderer selection must be forced {expected}")
    if not frames:
        errors.append("renderer report has no frame records")
    metric_names = (
        "texture_uploads", "texture_upload_bytes", "damage_rectangles",
        "readback_bytes", "texture_cache_bytes",
    )
    for index, frame in enumerate(frames):
        if frame.get("selected") != expected or frame.get("disposition") != "complete":
            errors.append(f"frame {index} is not a complete {expected} frame")
        if frame.get("error") is not None:
            errors.append(f"frame {index} reports an error")
        for metric in metric_names:
            if not integer(frame.get(metric)):
                errors.append(f"frame {index} has invalid {metric}")
    if expected == "software":
        for field in ("egl_platform", "egl_vendor", "egl_version", "gles_version",
                      "gl_vendor", "gl_renderer", "gl_version", "gbm_device",
                      "render_node"):
            if selection.get(field) is not None:
                errors.append(f"software selection must report null {field}")
        if any(frame.get("texture_uploads") or frame.get("texture_upload_bytes") or
               frame.get("readback_bytes") for frame in frames):
            errors.append("software frames must not claim GL upload/readback metrics")
    else:
        for field in ("egl_platform", "egl_vendor", "egl_version", "gles_version",
                      "gl_vendor", "gl_renderer", "gl_version"):
            if not nonempty(selection.get(field)):
                errors.append(f"GLES selection must record {field}")
        if selection.get("software_renderer") not in (True, False):
            errors.append("GLES selection must classify software_renderer")
        if selection.get("egl_platform") == "gbm":
            if not nonempty(selection.get("gbm_device")) or not nonempty(selection.get("render_node")):
                errors.append("GBM selection must record device and render node")
        elif selection.get("egl_platform") != "surfaceless":
            errors.append("GLES EGL platform must be gbm or surfaceless")
        if not any(frame.get("texture_uploads", 0) > 0 and
                   frame.get("texture_upload_bytes", 0) > 0 and
                   frame.get("readback_bytes", 0) > 0 and
                   frame.get("damage_rectangles", 0) > 0 for frame in frames):
            errors.append("GLES report has no damaged upload/readback frame")
    if errors:
        raise ValueError("; ".join(errors))
    return {
        "requested": selection["requested"],
        "selected": selection["selected"],
        "egl_platform": selection.get("egl_platform"),
        "egl_vendor": selection.get("egl_vendor"),
        "egl_version": selection.get("egl_version"),
        "gles_version": selection.get("gles_version"),
        "gl_vendor": selection.get("gl_vendor"),
        "gl_renderer": selection.get("gl_renderer"),
        "gl_version": selection.get("gl_version"),
        "gbm_device": selection.get("gbm_device"),
        "render_node": selection.get("render_node"),
        "software_renderer": selection.get("software_renderer"),
        "frame_count": len(frames),
        "texture_uploads": sum(item["texture_uploads"] for item in frames),
        "texture_upload_bytes": sum(item["texture_upload_bytes"] for item in frames),
        "damage_rectangles": sum(item["damage_rectangles"] for item in frames),
        "readback_bytes": sum(item["readback_bytes"] for item in frames),
        "peak_texture_cache_bytes": max(item["texture_cache_bytes"] for item in frames),
    }


def validate_drm(path: pathlib.Path) -> dict[str, Any]:
    values = records(path)
    copies = [item for item in values if item.get("record") == "damage-copy"]
    frames = [item for item in values if item.get("record") in {"modeset", "flip"}]
    vt = [item for item in values if item.get("record") == "vt"]
    restores = [item for item in values if item.get("record") == "restore"]
    errors: list[str] = []
    if not frames or any(item.get("canonical_hash") != item.get("scanout_hash") for item in frames):
        errors.append("every DRM frame must prove canonical/scanout hash parity")
    if not copies:
        errors.append("DRM report has no damage-copy records")
    reasons = {item.get("full_copy_reason") for item in copies}
    if "first-use" not in reasons:
        errors.append("DRM report lacks first-use full-copy evidence")
    if "vt-resume" not in reasons:
        errors.append("DRM report lacks VT-resume full-copy evidence")
    if not any(item.get("full_copy_reason") == "none" and
               integer(item.get("copied_bytes")) and
               integer(item.get("full_frame_bytes")) and
               item["copied_bytes"] < item["full_frame_bytes"] and
               isinstance(item.get("copy_rectangles"), list) and
               item["copy_rectangles"] for item in copies):
        errors.append("DRM report lacks a bounded partial damage copy")
    for item in copies:
        for field in ("generation", "buffer", "framebuffer_id", "full_frame_bytes",
                      "copied_bytes", "history_span", "cumulative_full_frame_bytes",
                      "cumulative_copied_bytes", "cumulative_copy_ratio_ppm"):
            if not integer(item.get(field)):
                errors.append(f"damage-copy has invalid {field}")
    transitions = [item.get("transition") for item in vt]
    if "release" not in transitions or "acquire" not in transitions:
        errors.append("DRM report lacks VT release/acquire records")
    if not any(item.get("transition") == "acquire" and item.get("master_owned") is True and
               item.get("full_modeset") is True for item in vt):
        errors.append("VT acquire did not prove master ownership and full modeset")
    if not restores or not all(restores[-1].get(field) is True for field in
                               ("kms", "vt", "master_drop", "framebuffer_cleanup")):
        errors.append("final DRM restore record is incomplete")
    if any(item.get("record") == "fatal" for item in values):
        errors.append("DRM report contains a fatal record")
    if errors:
        raise ValueError("; ".join(dict.fromkeys(errors)))
    return {
        "frame_count": len(frames),
        "damage_copy_count": len(copies),
        "copied_bytes": sum(item["copied_bytes"] for item in copies),
        "full_frame_bytes": sum(item["full_frame_bytes"] for item in copies),
        "partial_copy_count": sum(item.get("full_copy_reason") == "none" for item in copies),
        "full_copy_reasons": sorted(str(reason) for reason in reasons),
        "vt_transitions": transitions,
        "restored": True,
    }


def validate_sync(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text())
    errors: list[str] = []
    if value.get("schema") != 1:
        errors.append("sync observation schema must be one")
    counts = value.get("counts", {})
    for field in ("eventfd_before", "eventfd_live", "eventfd_after",
                  "shm_before", "shm_live", "shm_after", "live_processes_after"):
        if not integer(counts.get(field)):
            errors.append(f"sync observation has invalid {field}")
    if integer(counts.get("eventfd_before")) and integer(counts.get("eventfd_live")) and \
       counts["eventfd_live"] <= counts["eventfd_before"]:
        errors.append("live session did not expose additional eventfds")
    if counts.get("eventfd_after") != counts.get("eventfd_before"):
        errors.append("eventfd count did not return to baseline")
    if counts.get("shm_after") != counts.get("shm_before"):
        errors.append("SysV SHM inventory did not return to baseline")
    if counts.get("live_processes_after") != 0:
        errors.append("session cgroup retained live processes")
    checks = value.get("checks", {})
    required = {"eventfd_capability", "missing_token_wait", "read_after_token",
                "runtime_sockets_removed", "input_devices_removed",
                "texture_cache_released", "device_fds_released"}
    if set(checks) != required or any(checks.get(item) is not True for item in required):
        errors.append("sync/cleanup checks are incomplete")
    if errors:
        raise ValueError("; ".join(errors))
    return {"counts": counts, "checks": checks, "passed": True}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--software-renderer", type=pathlib.Path, required=True)
    parser.add_argument("--gles-renderer", type=pathlib.Path, required=True)
    parser.add_argument("--drm-report", type=pathlib.Path, required=True)
    parser.add_argument("--sync-observation", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        software = validate_renderer(arguments.software_renderer, "software")
        gles = validate_renderer(arguments.gles_renderer, "gles")
        drm = validate_drm(arguments.drm_report)
        sync = validate_sync(arguments.sync_observation)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"M12 runtime reports: {error}")
        return 1
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    outputs = {
        "milestone12-renderer-summary.json": {"schema": 1, "software": software, "gles": gles, "passed": True},
        "milestone12-drm-damage-summary.json": {"schema": 1, **drm, "passed": True},
        "milestone12-sync-report.json": {"schema": 1, **sync},
    }
    for name, value in outputs.items():
        (arguments.output_dir / name).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    print("M12 runtime reports: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
