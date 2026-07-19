#!/usr/bin/env python3
"""Validate M13 atomic headless frame-set records against their PPMs."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import sys
from typing import Any


HEX64 = re.compile(r"^[0-9a-f]{16}$")
TRANSFORMS = {
    "normal": 0,
    "rotate-90": 1,
    "rotate-180": 2,
    "rotate-270": 3,
    "flipped": 4,
    "flipped-90": 5,
    "flipped-180": 6,
    "flipped-270": 7,
}
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def fail(message: str) -> None:
    raise ValueError(message)


def append_byte(value: int, byte: int) -> int:
    return ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF


def append_integer(value: int, integer: int, width: int) -> int:
    for shift in range(0, width, 8):
        value = append_byte(value, (integer >> shift) & 0xFF)
    return value


def fnv1a64(payload: bytes) -> int:
    value = FNV_OFFSET
    for byte in payload:
        value = append_byte(value, byte)
    return value


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        fail(f"{path} is not an 8-bit binary PPM")
    width, height = (int(item) for item in match.groups())
    pixels = data[match.end():]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        fail(f"{path} has inconsistent PPM dimensions or payload")
    return width, height, pixels


def require_positive(value: Any, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(f"{field} must be a positive integer")
    return value


def require_u32(value: Any, field: str, *, positive: bool = True) -> int:
    if (not isinstance(value, int) or isinstance(value, bool)
            or value < (1 if positive else 0) or value > 0xFFFFFFFF):
        fail(f"{field} must be a valid 32-bit integer")
    return value


def aggregate_hash(record: dict[str, Any]) -> int:
    value = FNV_OFFSET
    for byte in b"glasswyrm-output-frame-set-v1":
        value = append_byte(value, byte)
    value = append_integer(value, record["layout_generation"], 64)
    value = append_integer(value, int(record["primary_output_id"], 16), 64)
    for output in sorted(record["outputs"], key=lambda item: int(item["output_id"], 16)):
        value = append_integer(value, int(output["output_id"], 16), 64)
        value = append_integer(value, output["physical"]["width"], 32)
        value = append_integer(value, output["physical"]["height"], 32)
        value = append_integer(value, output["scale"]["numerator"], 32)
        value = append_integer(value, output["scale"]["denominator"], 32)
        value = append_integer(value, TRANSFORMS[output["transform"]], 32)
        value = append_integer(value, int(output["fnv1a64"], 16), 64)
    return value


def locate_frame(root: pathlib.Path, name: Any) -> pathlib.Path:
    if (not isinstance(name, str) or not name or pathlib.PurePath(name).name != name):
        fail("frame-set output file must be a safe basename")
    matches = [path for path in root.rglob(name) if path.is_file()]
    if len(matches) != 1:
        fail(f"frame-set output {name!r} must resolve to exactly one PPM")
    return matches[0]


def validate_output(output: Any, root: pathlib.Path, label: str) -> int:
    if not isinstance(output, dict):
        fail(f"{label} is not an object")
    output_id = output.get("output_id")
    visible = output.get("fnv1a64")
    if not isinstance(output_id, str) or not HEX64.fullmatch(output_id):
        fail(f"{label}.output_id must be a lowercase 64-bit hash")
    if not isinstance(visible, str) or not HEX64.fullmatch(visible):
        fail(f"{label}.fnv1a64 must be a lowercase 64-bit hash")
    physical = output.get("physical")
    logical = output.get("logical")
    scale = output.get("scale")
    if not all(isinstance(item, dict) for item in (physical, logical, scale)):
        fail(f"{label} geometry or scale is missing")
    width = require_u32(physical.get("width"), f"{label}.physical.width")
    height = require_u32(physical.get("height"), f"{label}.physical.height")
    require_u32(logical.get("x"), f"{label}.logical.x", positive=False)
    require_u32(logical.get("y"), f"{label}.logical.y", positive=False)
    logical_width = require_u32(logical.get("width"), f"{label}.logical.width")
    logical_height = require_u32(logical.get("height"), f"{label}.logical.height")
    numerator = require_u32(scale.get("numerator"), f"{label}.scale.numerator")
    denominator = require_u32(scale.get("denominator"), f"{label}.scale.denominator")
    if math.gcd(numerator, denominator) != 1:
        fail(f"{label} scale must be reduced")
    if denominator > 120:
        fail(f"{label} scale denominator exceeds the M13 limit")
    if numerator < denominator or numerator > 4 * denominator:
        fail(f"{label} scale must be within 1/1 through 4/1")
    transform = output.get("transform")
    if transform not in TRANSFORMS:
        fail(f"{label}.transform is invalid")
    transformed_width, transformed_height = width, height
    if TRANSFORMS[transform] in {1, 3, 5, 7}:
        transformed_width, transformed_height = height, width
    expected_width = (transformed_width * denominator + numerator - 1) // numerator
    expected_height = (transformed_height * denominator + numerator - 1) // numerator
    if (logical_width, logical_height) != (expected_width, expected_height):
        fail(f"{label} logical geometry differs from scale and transform")
    damage = output.get("damage")
    if not isinstance(damage, list):
        fail(f"{label}.damage is not an array")
    for index, rectangle in enumerate(damage):
        if not isinstance(rectangle, dict):
            fail(f"{label}.damage[{index}] is not an object")
        x = require_u32(rectangle.get("x"), f"{label}.damage[{index}].x", positive=False)
        y = require_u32(rectangle.get("y"), f"{label}.damage[{index}].y", positive=False)
        rectangle_width = require_u32(rectangle.get("width"), f"{label}.damage[{index}].width")
        rectangle_height = require_u32(rectangle.get("height"), f"{label}.damage[{index}].height")
        if x + rectangle_width > width or y + rectangle_height > height:
            fail(f"{label}.damage[{index}] exceeds the physical output")
    path = locate_frame(root, output.get("file"))
    ppm_width, ppm_height, pixels = read_ppm(path)
    if (ppm_width, ppm_height) != (width, height):
        fail(f"{label} physical geometry differs from {path.name}")
    if f"{fnv1a64(pixels):016x}" != visible:
        fail(f"{label} visible hash differs from {path.name}")
    return int(output_id, 16)


def validate_manifest(manifest: pathlib.Path, dump_root: pathlib.Path) -> str:
    lines = manifest.read_text(encoding="utf-8").splitlines()
    if not lines:
        fail("frame-set manifest is empty")
    previous_ordinal = 0
    last_hash = ""
    for line_number, line in enumerate(lines, 1):
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            fail(f"frame-set record {line_number} is invalid JSON: {error}")
        if not isinstance(record, dict) or record.get("schema_version") != 13:
            fail(f"frame-set record {line_number} schema differs")
        ordinal = require_positive(record.get("transaction_ordinal"), "transaction_ordinal")
        if ordinal <= previous_ordinal:
            fail("frame-set transaction ordinals must increase")
        previous_ordinal = ordinal
        for field in ("commit_id", "generation", "layout_generation"):
            require_positive(record.get(field), field)
        primary = record.get("primary_output_id")
        aggregate = record.get("aggregate_hash")
        if not isinstance(primary, str) or not HEX64.fullmatch(primary):
            fail("primary_output_id must be a lowercase 64-bit hash")
        if not isinstance(aggregate, str) or not HEX64.fullmatch(aggregate):
            fail("aggregate_hash must be a lowercase 64-bit hash")
        outputs = record.get("outputs")
        if (not isinstance(outputs, list) or not 1 <= len(outputs) <= 8
                or record.get("output_count") != len(outputs)):
            fail("each M13 frame set must contain one through eight outputs")
        output_ids = [validate_output(output, dump_root,
                                      f"record {line_number} output {index}")
                      for index, output in enumerate(outputs)]
        if (len(set(output_ids)) != len(output_ids)
                or int(primary, 16) not in output_ids):
            fail("frame-set output identities or primary output are inconsistent")
        if output_ids != sorted(output_ids):
            fail("frame-set outputs must use stable ascending output-ID order")
        expected = f"{aggregate_hash(record):016x}"
        if aggregate != expected:
            fail(f"frame-set record {line_number} aggregate hash differs")
        last_hash = aggregate
    return last_hash


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("dump_root", type=pathlib.Path)
    parser.add_argument("--print-last-aggregate", action="store_true")
    arguments = parser.parse_args()
    try:
        last_hash = validate_manifest(arguments.manifest, arguments.dump_root)
    except (OSError, ValueError) as error:
        print(f"M13 frame-set validation failed: {error}", file=sys.stderr)
        return 1
    if arguments.print_last_aggregate:
        print(last_hash)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
