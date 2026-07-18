#!/usr/bin/env python3
"""Validate the checksum-protected Milestone 13 output fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import Any

import validate_frame_sets


BOOTSTRAP_FILES = {"README.md", "scale-client-result.schema.json"}
FIXTURE_FILES = {
    "output-inventory.json", "layout-before.json", "layout-after.json",
    "randr-little.json", "randr-big.json", "gw-scale-little.json",
    "gw-scale-big.json", "legacy-left.ppm", "legacy-right.ppm",
    "legacy-spanning-left.ppm", "legacy-spanning-right.ppm",
    "aware-left.ppm", "aware-right.ppm", "rotate90.ppm", "flipped.ppm",
    "frame-sets.jsonl", "gwinfo-outputs.json", "gwinfo-windows.json",
    "gwout-result.json", "scale-client-result.json",
    "renderer-fractional-diff.json",
}
SHA_LINE = re.compile(r"^([0-9a-f]{64})  ([^/]+)$")


def fail(message: str) -> None:
    raise ValueError(message)


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        fail(f"{path.name} is not a JSON object")
    return value


def parse_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        fail(f"{path.name} is not an 8-bit binary PPM")
    try:
        width, height = (int(value) for value in match.groups())
    except ValueError as error:
        fail(f"{path.name} has invalid PPM dimensions: {error}")
    if (width <= 0 or height <= 0
            or len(data) - match.end() != width * height * 3):
        fail(f"{path.name} has inconsistent PPM payload length")
    return width, height, data[match.end():]


def distinct_colors(pixels: bytes, limit: int = 3) -> int:
    colors: set[bytes] = set()
    for offset in range(0, len(pixels), 3):
        colors.add(pixels[offset:offset + 3])
        if len(colors) >= limit:
            break
    return len(colors)


def has_directional_content(width: int, height: int, pixels: bytes) -> bool:
    row_bytes = width * 3
    top = pixels[:(height // 2) * row_bytes]
    bottom = pixels[((height + 1) // 2) * row_bytes:]
    left = bytearray()
    right = bytearray()
    left_bytes = (width // 2) * 3
    right_offset = ((width + 1) // 2) * 3
    for row in range(height):
        start = row * row_bytes
        left.extend(pixels[start:start + left_bytes])
        right.extend(pixels[start + right_offset:start + row_bytes])
    return (hashlib.sha256(top).digest() != hashlib.sha256(bottom).digest()
            and hashlib.sha256(left).digest() != hashlib.sha256(right).digest())


def validate_scale_client(value: dict[str, Any]) -> None:
    if value.get("schema") != "glasswyrm.m13-scale-client.v1":
        fail("scale-client-result.json schema differs")
    if value.get("byte_order") not in {"little", "big"}:
        fail("scale-client-result.json byte order differs")
    if value.get("logical_geometry") != {"width": 320, "height": 240}:
        fail("scale-client logical geometry differs")
    if value.get("buffer_geometry") != {"width": 640, "height": 480}:
        fail("scale-client buffer geometry differs")
    if value.get("present_serial") != 1 or value.get("reset_scale") != 1:
        fail("scale-client present/reset contract differs")
    for state_name in ("initial", "moved"):
        state = value.get(state_name)
        if not isinstance(state, dict):
            fail(f"scale-client {state_name} state is missing")
        preferred = state.get("preferred")
        memberships = state.get("memberships")
        if (not isinstance(state.get("primary"), int)
                or not isinstance(preferred, list) or len(preferred) != 2
                or not all(isinstance(item, int) and item > 0
                           for item in preferred)
                or not isinstance(memberships, list)
                or not all(isinstance(item, int) and item > 0
                           for item in memberships)):
            fail(f"scale-client {state_name} state differs")


def validate_complete(root: pathlib.Path) -> None:
    inventory = load_object(root / "output-inventory.json")
    outputs = inventory.get("outputs")
    if (not isinstance(outputs, list) or len(outputs) != 2
            or [item.get("name") for item in outputs
                if isinstance(item, dict)] != ["LEFT", "RIGHT"]
            or len({item.get("id") for item in outputs}) != 2):
        fail("output inventory must contain stable LEFT and RIGHT outputs")

    before = load_object(root / "layout-before.json")
    after = load_object(root / "layout-after.json")
    before_generation = before.get("layout_generation", before.get("generation"))
    after_generation = after.get("layout_generation", after.get("generation"))
    before_root = (before.get("root_width"), before.get("root_height"))
    after_root = (after.get("root_width"), after.get("root_height"))
    if before_root == (None, None):
        before_root = tuple(before.get("root", {}).get(key)
                            for key in ("width", "height"))
    if after_root == (None, None):
        after_root = tuple(after.get("root", {}).get(key)
                           for key in ("width", "height"))
    if (not isinstance(before_generation, int)
            or after_generation != before_generation + 1
            or before_root != (1440, 600) or after_root != (1280, 480)):
        fail("canonical layout generation or root geometry differs")

    for protocol in ("randr", "gw-scale"):
        for byte_order in ("little", "big"):
            value = load_object(root / f"{protocol}-{byte_order}.json")
            if (value.get("byte_order") != byte_order
                    or value.get("passed") is not True
                    or value.get("errors") not in (None, [])):
                fail(f"{protocol} {byte_order} probe differs")

    gwinfo_outputs = load_object(root / "gwinfo-outputs.json")
    if (not isinstance(gwinfo_outputs.get("layout_generation"), int)
            or len(gwinfo_outputs.get("outputs", [])) != 2):
        fail("gwinfo-outputs.json is not complete evidence")
    gwinfo_windows = load_object(root / "gwinfo-windows.json")
    if (not isinstance(gwinfo_windows.get("layout_generation"), int)
            or not isinstance(gwinfo_windows.get("windows"), list)):
        fail("gwinfo-windows.json is not complete evidence")
    gwout = load_object(root / "gwout-result.json")
    if (gwout.get("result") not in (0, None)
            or (gwout.get("passed") is not True and
                not isinstance(gwout.get("applied_generation"), int))):
        fail("gwout-result.json is not an accepted commit")
    validate_scale_client(load_object(root / "scale-client-result.json"))

    difference = load_object(root / "renderer-fractional-diff.json")
    maximum = difference.get("maximum_channel_difference")
    if (difference.get("passed") is not True
            or not isinstance(maximum, int) or maximum < 0 or maximum > 1):
        fail("fractional renderer comparison exceeds one channel value")

    validate_frame_sets.validate_manifest(root / "frame-sets.jsonl", root)

    left_frames = {"legacy-left.ppm", "legacy-spanning-left.ppm",
                   "aware-left.ppm"}
    right_frames = {"legacy-right.ppm", "legacy-spanning-right.ppm",
                    "aware-right.ppm", "rotate90.ppm", "flipped.ppm"}
    frames: dict[str, tuple[int, int, bytes]] = {}
    for name in sorted(left_frames | right_frames):
        frames[name] = parse_ppm(root / name)
        if not any(frames[name][2]):
            fail(f"{name} is an all-black frame")
    for name in left_frames:
        if frames[name][:2] != (640, 480):
            fail(f"{name} must use the LEFT 640x480 native scanout")
    for name in right_frames:
        if frames[name][:2] != (800, 600):
            fail(f"{name} must use the RIGHT 800x600 native scanout")
    for group in (left_frames, right_frames):
        hashes = {hashlib.sha256(frames[name][2]).digest() for name in group}
        if len(hashes) != len(group):
            fail("scenario PPM fixtures must have distinct visible content")
    for name in ("rotate90.ppm", "flipped.ppm"):
        width, height, pixels = frames[name]
        if distinct_colors(pixels) < 3:
            fail(f"{name} lacks a directional multi-color transform fixture")
        if not has_directional_content(width, height, pixels):
            fail(f"{name} lacks distinct horizontal and vertical content")


def validate(root: pathlib.Path, *, require_complete: bool = False) -> bool:
    sums = root / "SHA256SUMS"
    if not sums.is_file():
        fail("SHA256SUMS is missing")
    listed: dict[str, str] = {}
    for number, line in enumerate(sums.read_text(encoding="ascii").splitlines(), 1):
        match = SHA_LINE.fullmatch(line)
        if not match:
            fail(f"invalid SHA256SUMS line {number}")
        expected, name = match.groups()
        if name in listed:
            fail(f"duplicate checksum entry {name}")
        path = root / name
        if not path.is_file() or digest(path) != expected:
            fail(f"checksum mismatch for {name}")
        listed[name] = expected
    complete = bool(set(listed) & FIXTURE_FILES)
    if require_complete and not complete:
        fail("complete M13 fixtures are required")
    expected = BOOTSTRAP_FILES | FIXTURE_FILES if complete else BOOTSTRAP_FILES
    if set(listed) != expected:
        fail("checksum inventory differs")
    schema = load_object(root / "scale-client-result.schema.json")
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        fail("scale-client schema declaration differs")
    if complete:
        validate_complete(root)
    return complete


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "root", nargs="?", type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2] / "fixtures" / "m13",
    )
    parser.add_argument("--require-complete", action="store_true")
    arguments = parser.parse_args()
    try:
        complete = validate(arguments.root, require_complete=arguments.require_complete)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"m13 fixtures: {error}", file=sys.stderr)
        return 1
    print("m13 fixtures: passed " + ("complete" if complete else "bootstrap"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
