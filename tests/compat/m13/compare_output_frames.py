#!/usr/bin/env python3
"""Compare canonical software/GLES P6 output frames with a one-value bound."""

from __future__ import annotations

import argparse
import json
import pathlib
import re


HEADER = re.compile(rb"P6\s+(\d+)\s+(\d+)\s+255\s")


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = HEADER.match(data)
    if not match:
        raise ValueError(f"{path} is not an 8-bit P6 PPM")
    width, height = (int(value) for value in match.groups())
    pixels = data[match.end():]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path} has an inconsistent payload")
    return width, height, pixels


def latest(directory: pathlib.Path) -> list[pathlib.Path]:
    paths = sorted(directory.glob("*.ppm"))
    if len(paths) < 2:
        raise ValueError(f"{directory} has fewer than two output frames")
    # Headless frame names finish with the stable output ID. Retain the newest
    # generation for each ID without depending on timestamps.
    selected: dict[str, pathlib.Path] = {}
    for path in paths:
        identifier = path.stem.rsplit("-", 1)[-1]
        selected[identifier] = path
    if len(selected) != 2:
        raise ValueError(f"{directory} does not contain exactly two output IDs")
    return [selected[key] for key in sorted(selected)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--software-dir", required=True, type=pathlib.Path)
    parser.add_argument("--gles-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    try:
        software = latest(arguments.software_dir)
        gles = latest(arguments.gles_dir)
        records = []
        maximum = 0
        for software_path, gles_path in zip(software, gles, strict=True):
            software_id = software_path.stem.rsplit("-", 1)[-1]
            gles_id = gles_path.stem.rsplit("-", 1)[-1]
            if software_id != gles_id:
                raise ValueError("software/GLES stable output identities differ")
            sw_width, sw_height, sw_pixels = read_ppm(software_path)
            gl_width, gl_height, gl_pixels = read_ppm(gles_path)
            if (sw_width, sw_height) != (gl_width, gl_height):
                raise ValueError("software/GLES output dimensions differ")
            difference = max(abs(left - right)
                             for left, right in zip(sw_pixels, gl_pixels,
                                                   strict=True))
            maximum = max(maximum, difference)
            records.append({"output": software_id,
                            "width": sw_width, "height": sw_height,
                            "maximum_channel_difference": difference})
        payload = {"schema": 1, "passed": maximum <= 1,
                   "maximum_channel_difference": maximum, "outputs": records}
    except (OSError, ValueError) as error:
        payload = {"schema": 1, "passed": False,
                   "maximum_channel_difference": None, "outputs": [],
                   "error": str(error)}
    arguments.output.write_text(json.dumps(payload, indent=2,
                                           sort_keys=True) + "\n")
    return 0 if payload["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
