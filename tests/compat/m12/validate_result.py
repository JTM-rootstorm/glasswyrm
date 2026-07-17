#!/usr/bin/env python3
"""Strict structural validator for repository-owned M12 result JSON."""

from __future__ import annotations

import argparse
import json
import pathlib


SDL_CHECKS = {
    "initialized", "x11_driver", "display_mode", "window_created",
    "surface_updated", "clipboard_round_trip", "custom_cursor",
    "fullscreen_entered", "fullscreen_exited", "geometry_restored",
    "borderless", "close_event",
}
XCB_CHECKS = {
    "registry", "big_requests", "mit_shm", "xfixes_version",
    "selection_notify", "region_algebra", "damage_version",
    "damage_notify", "damage_subtract", "render_version",
    "composite_version", "randr_version",
}
RAW_SCENARIOS = {"registry", "big-requests", "xfixes-damage", "errors"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=pathlib.Path)
    parser.add_argument("--allow-failed", action="store_true")
    arguments = parser.parse_args()
    errors: list[str] = []
    try:
        result = json.loads(arguments.result.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"invalid result JSON: {error}")
        return 2
    if result.get("schema") != 1:
        errors.append("schema must be 1")
    probe = result.get("probe")
    checks = result.get("checks")
    if not isinstance(checks, dict) or not checks:
        errors.append("checks must be a nonempty object")
        checks = {}
    if probe == "m12_sdl_probe":
        if set(checks) != SDL_CHECKS:
            errors.append("SDL check set differs from the frozen schema")
        if result.get("video_driver") != "x11":
            errors.append("SDL video_driver must be x11")
        if result.get("display_count") != 1:
            errors.append("SDL display_count must be one")
    elif probe == "m12_xcb_probe":
        if set(checks) != XCB_CHECKS:
            errors.append("XCB check set differs from the frozen schema")
    elif probe == "m12_raw_probe":
        if result.get("byte_order") not in {"little", "big"}:
            errors.append("raw byte_order is invalid")
        if result.get("scenario") not in RAW_SCENARIOS:
            errors.append("raw scenario is invalid")
    elif probe is None and result.get("profile") in {"shm", "no-shm"}:
        workloads = result.get("workloads")
        if not isinstance(workloads, list) or not workloads:
            errors.append("workload result has no workloads")
        if not isinstance(result.get("evidence_errors"), list):
            errors.append("workload evidence_errors must be a list")
    else:
        errors.append("unknown M12 result kind")
    if any(not isinstance(value, bool) for value in checks.values()):
        errors.append("all checks must be booleans")
    if result.get("passed") is not True and not arguments.allow_failed:
        errors.append("result must be passed")
    if result.get("passed") is True and not all(checks.values()):
        errors.append("passed result contains a failed check")
    if errors:
        for error in errors:
            print(f"m12 result: {error}")
        return 1
    print(f"m12 result: passed ({arguments.result})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
