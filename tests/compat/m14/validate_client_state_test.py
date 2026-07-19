#!/usr/bin/env python3

import copy
import json
from pathlib import Path
import tempfile

from validate_client_state import validate, validate_path


def valid_state() -> dict[str, object]:
    return {
        "schema": "glasswyrm.m14-vrr-client.v2",
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
        "events_selected": True,
        "preference_reply_count": 0,
        "notify_event_count": 0,
        "notify_change_mask": 0,
        "reason_mask": 0,
        "eventfd_synchronized": True,
        "preference_sequence": [],
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
    requested["eventfd_synchronized"] = False
    requested["preference_reply_count"] = 1
    requested["notify_event_count"] = 1
    requested["frame_count"] = 1
    validate(requested)
    preference = valid_state()
    preference.update(
        mode="preference",
        preference="Disable",
        fullscreen_requested=False,
        cadence_absolute_monotonic=False,
        eventfd_synchronized=False,
        frame_count=1,
        preference_reply_count=4,
        notify_event_count=3,
        notify_change_mask=1,
        reason_mask=1 << 19,
        preference_sequence=["Default", "Allow", "Prefer", "Disable"],
    )
    validate(preference)
    rejected(lambda value: value.update(command="anything"))
    rejected(lambda value: value.update(frame_count=10_001))
    rejected(lambda value: value.update(target_interval_nanoseconds=13_888_889))
    rejected(lambda value: value.update(fullscreen_requested=False))
    rejected(lambda value: value.update(bounded_damage_width=65))
    rejected(lambda value: value.update(events_selected=False))
    rejected(lambda value: value.update(notify_change_mask=8))
    rejected(lambda value: value.update(reason_mask=1 << 40))
    rejected(lambda value: value.update(eventfd_synchronized=False))
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
