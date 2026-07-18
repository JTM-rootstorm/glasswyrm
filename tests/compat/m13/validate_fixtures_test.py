#!/usr/bin/env python3
"""Unit coverage for the Milestone 13 fixture validator."""

from __future__ import annotations

import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile


FILES = {
    "output-inventory.json": {
        "outputs": [{"id": 1, "name": "LEFT"}, {"id": 2, "name": "RIGHT"}]},
    "layout-before.json": {"generation": 7, "root": {"width": 1440, "height": 600}},
    "layout-after.json": {"generation": 8, "root": {"width": 1280, "height": 480}},
    "randr-little.json": {"byte_order": "little", "passed": True, "errors": []},
    "randr-big.json": {"byte_order": "big", "passed": True, "errors": []},
    "gw-scale-little.json": {"byte_order": "little", "passed": True, "errors": []},
    "gw-scale-big.json": {"byte_order": "big", "passed": True, "errors": []},
    "gwinfo-outputs.json": {"layout_generation": 8, "outputs": [{}, {}]},
    "gwinfo-windows.json": {"layout_generation": 8, "windows": []},
    "gwout-result.json": {"result": 0, "applied_generation": 8},
    "renderer-fractional-diff.json": {
        "passed": True, "maximum_channel_difference": 1},
    "scale-client-result.json": {
        "schema": "glasswyrm.m13-scale-client.v1", "byte_order": "little",
        "window": 17, "logical_geometry": {"width": 320, "height": 240},
        "buffer_geometry": {"width": 640, "height": 480},
        "initial": {"primary": 1, "preferred": [1, 1], "memberships": [1]},
        "output": {"id": 1, "logical": {}, "physical": {}, "scale": [1, 1]},
        "notification": {"reasons": ["membership"], "primary": 2},
        "moved": {"primary": 2, "preferred": [2, 1], "memberships": [2]},
        "present_serial": 1, "reset_scale": 1,
    },
}
PPMS = (
    "legacy-left.ppm", "legacy-right.ppm", "legacy-spanning-left.ppm",
    "legacy-spanning-right.ppm", "aware-left.ppm", "aware-right.ppm",
    "rotate90.ppm", "flipped.ppm",
)


def checksum(directory: pathlib.Path) -> None:
    names = sorted(path.name for path in directory.iterdir()
                   if path.is_file() and path.name != "SHA256SUMS")
    (directory / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256((directory / name).read_bytes()).hexdigest()}  {name}\n"
        for name in names), encoding="ascii")


def write_ppm(path: pathlib.Path, width: int, height: int, seed: int) -> None:
    colors = [bytes(((seed + offset) % 255 + 1,
                     (seed * 3 + offset * 5) % 255 + 1,
                     (seed * 7 + offset * 11) % 255 + 1))
              for offset in range(4)]
    upper = colors[0] * (width // 2) + colors[1] * (width - width // 2)
    lower = colors[2] * (width // 2) + colors[3] * (width - width // 2)
    payload = upper * (height // 2) + lower * (height - height // 2)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + payload)


def run(validator: pathlib.Path, directory: pathlib.Path,
        expected: bool, message: str = "", require_complete: bool = False) -> None:
    command = [validator, directory]
    if require_complete:
        command.append("--require-complete")
    result = subprocess.run(command, text=True,
                            capture_output=True, check=False)
    assert (result.returncode == 0) is expected, result.stderr
    if message:
        assert message in result.stderr, result.stderr


with tempfile.TemporaryDirectory() as name:
    root = pathlib.Path(name)
    source = pathlib.Path(sys.argv[2])
    shutil.copyfile(source / "README.md", root / "README.md")
    shutil.copyfile(source / "scale-client-result.schema.json",
                    root / "scale-client-result.schema.json")
    checksum(root)
    validator = pathlib.Path(sys.argv[1])
    run(validator, root, True)
    run(validator, root, False, "complete M13 fixtures are required",
        require_complete=True)

    for filename, value in FILES.items():
        (root / filename).write_text(json.dumps(value) + "\n", encoding="utf-8")
    for seed, filename in enumerate(PPMS, 1):
        width, height = ((640, 480) if "left" in filename
                         else (800, 600))
        write_ppm(root / filename, width, height, seed)
    (root / "frame-sets.jsonl").write_text(json.dumps({
        "generation": 8, "aggregate_hash": "0123456789abcdef",
        "outputs": [{"id": 1}, {"id": 2}],
    }) + "\n", encoding="utf-8")
    checksum(root)
    run(validator, root, True)
    run(validator, root, True, require_complete=True)

    bad = json.loads((root / "renderer-fractional-diff.json").read_text())
    bad["maximum_channel_difference"] = 2
    (root / "renderer-fractional-diff.json").write_text(json.dumps(bad) + "\n")
    checksum(root)
    run(validator, root, False, "exceeds one channel value")

    bad["maximum_channel_difference"] = 1
    (root / "renderer-fractional-diff.json").write_text(json.dumps(bad) + "\n")
    checksum(root)
    (root / "aware-left.ppm").write_bytes(b"P6\n2 1\n255\n" + bytes(5))
    run(validator, root, False, "checksum mismatch")

    write_ppm(root / "aware-left.ppm", 800, 600, 20)
    checksum(root)
    run(validator, root, False, "must use the LEFT 640x480 native scanout")

    write_ppm(root / "aware-left.ppm", 640, 480, 5)
    write_ppm(root / "legacy-left.ppm", 640, 480, 5)
    checksum(root)
    run(validator, root, False, "distinct visible content")

    write_ppm(root / "legacy-left.ppm", 640, 480, 1)
    width, height = 800, 600
    (root / "rotate90.ppm").write_bytes(
        f"P6\n{width} {height}\n255\n".encode()
        + b"\x10\x20\x30" * (width * height))
    checksum(root)
    run(validator, root, False, "directional multi-color")
