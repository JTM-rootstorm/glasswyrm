#!/usr/bin/env python3
"""Validate the disposable M14 headless cadence and damage gate."""

from __future__ import annotations

import json
from pathlib import Path
import sys


def records(path: Path) -> list[dict[str, object]]:
    values: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path.name}:{number}: {error}") from error
            if not isinstance(value, dict):
                raise ValueError(f"{path.name}:{number}: record is not an object")
            values.append(value)
    if not values:
        raise ValueError(f"{path.name}: report is empty")
    return values


def main() -> int:
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} CLIENT VRR RENDERER FRAME_SETS", file=sys.stderr)
        return 2
    client = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    if (client.get("schema") != "glasswyrm.m14-vrr-client.v3"
            or client.get("selected_output") != "DP-1"
            or client.get("scheduled_frame_count") != 180
            or client.get("submitted_frame_count") != 180
            or not 121 <= client.get("presented_frame_count", -1) <= 180
            or client.get("maximum_outstanding_updates") != 1
            or client.get("presentation_paced") is not True):
        raise ValueError("client lacks the exact presentation-paced contract")

    vrr = records(Path(sys.argv[2]))
    timings = [value for value in vrr if value.get("record") == "timing"]
    markers = {(value.get("commit_id"), value.get("generation"))
               for value in timings}
    if len(timings) < 181 or len(markers) < 181:
        raise ValueError("headless report lacks 180 distinct cadence presentations")
    if any(value.get("simulated") is not True for value in timings):
        raise ValueError("headless timing record is not explicitly simulated")
    if not any(value.get("record") == "summary" for value in vrr) \
            or not any(value.get("record") == "restore" for value in vrr):
        raise ValueError("headless VRR report lacks shutdown summary records")

    renderer = records(Path(sys.argv[3]))
    output_frames = [value for value in renderer
                     if value.get("record") == "output-frame"
                     and value.get("disposition") == "complete"]
    if len(output_frames) < 181:
        raise ValueError("renderer report lacks the complete cadence frame sequence")
    first_commit = client["first_observed_commit_id"]
    last_commit = client["last_observed_commit_id"]
    steady = [value for value in output_frames
              if first_commit < value.get("commit_id", 0) <= last_commit]
    if len(steady) != 180:
        raise ValueError("renderer report does not match the client marker range")
    total_render_nanoseconds = 0
    total_hash_nanoseconds = 0
    for value in steady:
        outputs = value.get("outputs")
        if not isinstance(outputs, list) or len(outputs) != 1:
            raise ValueError("renderer cadence record lacks the exact output")
        output = outputs[0]
        damage = output.get("physical_damage_rectangles") \
            if isinstance(output, dict) else None
        if not isinstance(damage, list) or not damage:
            raise ValueError("renderer cadence record lacks bounded damage")
        if (output.get("rendered_pixels") != 64 * 64
                or output.get("frame_hash_bytes") != 2560 * 1440 * 3
                or isinstance(output.get("render_nanoseconds"), bool)
                or not isinstance(output.get("render_nanoseconds"), int)
                or output["render_nanoseconds"] < 0
                or isinstance(output.get("frame_hash_nanoseconds"), bool)
                or not isinstance(output.get("frame_hash_nanoseconds"), int)
                or output["frame_hash_nanoseconds"] < 0):
            raise ValueError("renderer scalar counters are incomplete")
        total_render_nanoseconds += output["render_nanoseconds"]
        total_hash_nanoseconds += output["frame_hash_nanoseconds"]
        for rectangle in damage:
            if (not isinstance(rectangle, dict)
                    or rectangle.get("width", 0) > 66
                    or rectangle.get("height", 0) > 66):
                raise ValueError("steady renderer damage exceeded the filter footprint")

    frame_sets = records(Path(sys.argv[4]))
    if len(frame_sets) < 181:
        raise ValueError("headless frame-set manifest lacks cadence presentations")
    steady_frame_sets = [value for value in frame_sets
                         if first_commit < value.get("commit_id", 0) <= last_commit]
    if len(steady_frame_sets) != 180:
        raise ValueError("frame-set manifest does not match the client marker range")
    for value in steady_frame_sets:
        outputs = value.get("outputs")
        physical = outputs[0].get("physical") \
            if isinstance(outputs, list) and len(outputs) == 1 \
            and isinstance(outputs[0], dict) else None
        if physical != {"width": 2560, "height": 1440}:
            raise ValueError("headless cadence output is not 2560x1440")
    print("m14 integrated cadence: valid (headless and fake DRM only); "
          f"render_ns={total_render_nanoseconds} "
          f"frame_hash_ns={total_hash_nanoseconds}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"m14 integrated cadence: {error}", file=sys.stderr)
        raise SystemExit(1)
