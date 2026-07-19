#!/usr/bin/env python3

import copy
import json
from pathlib import Path
import tempfile

from validate_client_state import validate, validate_path


def valid_state() -> dict[str, object]:
    return {
        "schema": "glasswyrm.m14-vrr-client.v1",
        "mode": "cadence",
        "window": 42,
        "width": 640,
        "height": 480,
        "preference": "Default",
        "fullscreen_requested": True,
        "borderless": False,
        "frame_count": 120,
        "target_refresh_hz": 72,
        "target_interval_nanoseconds": 13_888_888,
        "cadence_absolute_monotonic": True,
        "bounded_damage_width": 64,
        "bounded_damage_height": 64,
    }


def rejected(change) -> None:
    value = copy.deepcopy(valid_state())
    change(value)
    try:
        validate(value)
    except ValueError:
        return
    raise AssertionError("invalid client state was accepted")


def main() -> int:
    validate(valid_state())
    requested = valid_state()
    requested["mode"] = "app-requested"
    requested["preference"] = "Prefer"
    requested["fullscreen_requested"] = False
    requested["cadence_absolute_monotonic"] = False
    requested["frame_count"] = 1
    validate(requested)
    rejected(lambda value: value.update(command="anything"))
    rejected(lambda value: value.update(frame_count=10_001))
    rejected(lambda value: value.update(target_interval_nanoseconds=13_888_889))
    rejected(lambda value: value.update(fullscreen_requested=False))
    rejected(lambda value: value.update(bounded_damage_width=65))
    rejected(
        lambda value: value.update(
            mode="windowed",
            fullscreen_requested=False,
            cadence_absolute_monotonic=False,
            frame_count=2,
        )
    )
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        state_path = root / "state.json"
        state_path.write_text(json.dumps(valid_state()), encoding="utf-8")
        validate_path(state_path)
        symlink_path = root / "state-link.json"
        symlink_path.symlink_to(state_path)
        try:
            validate_path(symlink_path)
        except ValueError:
            pass
        else:
            raise AssertionError("symlinked client state was accepted")
    print("m14 client-state validator test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
