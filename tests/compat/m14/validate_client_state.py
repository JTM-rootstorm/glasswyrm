#!/usr/bin/env python3
"""Validate bounded m14_vrr_client state; this does not validate hardware."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import sys


KEYS = {
    "schema",
    "mode",
    "window",
    "width",
    "height",
    "preference",
    "fullscreen_requested",
    "borderless",
    "frame_count",
    "target_refresh_hz",
    "target_interval_nanoseconds",
    "cadence_absolute_monotonic",
    "bounded_damage_width",
    "bounded_damage_height",
}
MODES = {"fullscreen", "borderless", "windowed", "app-requested", "cadence"}


def validate(value: object) -> None:
    if not isinstance(value, dict) or set(value) != KEYS:
        raise ValueError("client state must contain exactly the v1 fields")
    if value["schema"] != "glasswyrm.m14-vrr-client.v1":
        raise ValueError("unknown client-state schema")
    mode = value["mode"]
    if mode not in MODES:
        raise ValueError("unknown client mode")
    for name in (
        "window",
        "width",
        "height",
        "frame_count",
        "target_refresh_hz",
        "target_interval_nanoseconds",
        "bounded_damage_width",
        "bounded_damage_height",
    ):
        if isinstance(value[name], bool) or not isinstance(value[name], int):
            raise ValueError(f"{name} must be an integer")
    for name in ("fullscreen_requested", "borderless", "cadence_absolute_monotonic"):
        if not isinstance(value[name], bool):
            raise ValueError(f"{name} must be boolean")
    if not (0 < value["window"] <= 0xFFFFFFFF):
        raise ValueError("window must be a nonzero XID")
    if not (64 <= value["width"] <= 0xFFFF and 64 <= value["height"] <= 0xFFFF):
        raise ValueError("window dimensions are outside the bounded range")
    if value["preference"] not in {"Default", "Prefer"}:
        raise ValueError("client preference must be Default or Prefer")
    if mode == "app-requested" and value["preference"] != "Prefer":
        raise ValueError("app-requested mode must explicitly use Prefer")
    if value["fullscreen_requested"] != (mode in {"fullscreen", "cadence"}):
        raise ValueError("fullscreen transition does not match mode")
    if value["borderless"] != (mode == "borderless"):
        raise ValueError("borderless state does not match mode")
    if value["cadence_absolute_monotonic"] != (mode == "cadence"):
        raise ValueError("cadence clock declaration does not match mode")
    if not (1 <= value["frame_count"] <= 10_000):
        raise ValueError("frame count is outside the bounded range")
    if mode != "cadence" and value["frame_count"] != 1:
        raise ValueError("only cadence mode may publish multiple frames")
    refresh = value["target_refresh_hz"]
    if not (1 <= refresh <= 1_000):
        raise ValueError("target refresh is outside the bounded range")
    if value["target_interval_nanoseconds"] != 1_000_000_000 // refresh:
        raise ValueError("target interval does not match integer cadence")
    if value["bounded_damage_width"] != 64 or value["bounded_damage_height"] != 64:
        raise ValueError("cadence damage rectangle changed")


def validate_path(path: Path) -> None:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as error:
        raise ValueError(f"client state is unavailable: {error}") from error
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode) or status.st_size > 16 * 1024:
            raise ValueError("client state must be a bounded regular non-symlink file")
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = -1
            contents = stream.read(16 * 1024 + 1)
        if len(contents) > 16 * 1024:
            raise ValueError("client state exceeds 16384 bytes")
        value = json.loads(contents.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid client-state JSON: {error}") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    validate(value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("state", type=Path)
    options = parser.parse_args()
    try:
        validate_path(options.state)
    except ValueError as error:
        print(f"m14 client state: {error}", file=sys.stderr)
        return 1
    print("m14 client state: valid (not hardware proof)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
