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
    "damage_notify", "damage_subtract", "render_version", "render_formats",
    "render_exact_pixels", "composite_version", "composite_named_lifetime",
    "randr_version", "randr_reporting", "recoverable_errors",
}
XCB_EVIDENCE_KEYS = {
    "registry_major", "registry_event", "registry_error",
    "render_over_pixel", "composite_named_pixel",
    "randr_output", "randr_crtc", "randr_mode",
}
RAW_SCENARIOS = {
    "registry", "big-requests", "shm", "xfixes-damage",
    "render-composite", "randr", "errors",
}

RAW_CHECKS = {
    "registry": {
        "BIG-REQUESTS", "MIT-SHM", "XFIXES", "DAMAGE", "RENDER",
        "Composite", "RANDR", "stable_list", "unknown_absent",
    },
    "big-requests": {"enable", "extended_put_image"},
    "shm": {"query_version", "attach_put_get_detach", "completion_event"},
    "xfixes-damage": {
        "xfixes_version", "damage_version", "region_algebra",
        "selection_notify", "damage_notify", "damage_subtract",
    },
    "render-composite": {
        "render_version", "render_formats", "render_exact_over",
        "composite_version", "composite_named_lifetime",
    },
    "randr": {
        "version", "resources", "output_crtc_primary",
        "idempotent_and_rejected_config", "bad_output_recoverable",
    },
    "errors": {
        "bad_region", "bad_damage", "unsupported_minor", "malformed_length",
        "malformed_client_isolated", "connection_continues",
    },
}

RAW_EVIDENCE_KEYS = {
    "registry": {"assignments", "list_extensions"},
    "big-requests": {"maximum_units", "upload_bytes"},
    "shm": {"version", "event_base", "error_base", "round_trip_xrgb"},
    "xfixes-damage": {"xfixes_version", "damage_version", "region_extents", "damage_id"},
    "render-composite": {"render_formats", "render_over_pixel", "composite_named_pixels_sha_sample"},
    "randr": {"version", "event_base", "error_base", "output", "crtc", "mode", "output_name"},
    "errors": {"bad_region", "bad_damage", "unsupported_minor", "bad_length"},
}

WORKLOAD_ENVIRONMENT = {
    "LC_ALL": "C",
    "LANG": "C",
    "XMODIFIERS": "@im=none",
    "SESSION_MANAGER": "",
    "XAUTHORITY": "/dev/null",
    "DISPLAY": ":99",
    "SDL_VIDEODRIVER": "x11",
    "SDL_RENDER_DRIVER": "software",
    "SDL_AUDIODRIVER": "dummy",
}


def validate_workload_environment(result: dict, errors: list[str]) -> None:
    environment = result.get("environment")
    if not isinstance(environment, dict):
        errors.append("workload environment must be an object")
        return
    if set(environment) != {*WORKLOAD_ENVIRONMENT, "LD_PRELOAD"}:
        errors.append("workload environment fields differ from the frozen profile")
        return
    for key, expected in WORKLOAD_ENVIRONMENT.items():
        if environment.get(key) != expected:
            errors.append(f"workload environment has invalid {key}")
    preload = environment.get("LD_PRELOAD")
    if not isinstance(preload, str) or not preload.endswith(
        "/tests/libgw_m9_fixed_time.so"
    ):
        errors.append("workload LD_PRELOAD is not the repository fixed-time shim")


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
        evidence = result.get("evidence")
        if not isinstance(evidence, dict) or set(evidence) != XCB_EVIDENCE_KEYS:
            errors.append("XCB evidence fields differ from the frozen schema")
    elif probe == "m12_raw_probe":
        if result.get("byte_order") not in {"little", "big"}:
            errors.append("raw byte_order is invalid")
        if result.get("scenario") not in RAW_SCENARIOS:
            errors.append("raw scenario is invalid")
        scenario = result.get("scenario")
        if scenario in RAW_CHECKS and set(checks) != RAW_CHECKS[scenario]:
            errors.append("raw check fields differ from the frozen scenario schema")
        evidence = result.get("evidence")
        if scenario in RAW_EVIDENCE_KEYS and (
            not isinstance(evidence, dict)
            or set(evidence) != RAW_EVIDENCE_KEYS[scenario]
        ):
            errors.append("raw evidence fields differ from the frozen scenario schema")
    elif probe is None and result.get("profile") in {"shm", "no-shm"}:
        workloads = result.get("workloads")
        if not isinstance(workloads, list) or not workloads:
            errors.append("workload result has no workloads")
        if not isinstance(result.get("evidence_errors"), list):
            errors.append("workload evidence_errors must be a list")
        validate_workload_environment(result, errors)
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
