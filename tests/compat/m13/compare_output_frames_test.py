#!/usr/bin/env python3
"""Unit coverage for the M13 software/GLES output comparator."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def ppm(pixel: tuple[int, int, int]) -> bytes:
    return b"P6\n1 1\n255\n" + bytes(pixel)


with tempfile.TemporaryDirectory() as name:
    root = pathlib.Path(name)
    software = root / "software"
    gles = root / "gles"
    software.mkdir()
    gles.mkdir()
    for identifier, pixel in (("0001", (10, 20, 30)),
                              ("0002", (40, 50, 60))):
        (software / f"frame-1-{identifier}.ppm").write_bytes(ppm(pixel))
        (gles / f"frame-1-{identifier}.ppm").write_bytes(
            ppm((pixel[0] + 1, pixel[1], pixel[2])))
    output = root / "result.json"
    command = [sys.argv[1], "--software-dir", str(software),
               "--gles-dir", str(gles), "--output", str(output)]
    subprocess.run(command, check=True)
    value = json.loads(output.read_text())
    assert value["passed"] is True
    assert value["maximum_channel_difference"] == 1

    (gles / "frame-1-0002.ppm").write_bytes(ppm((44, 50, 60)))
    result = subprocess.run(command, check=False)
    assert result.returncode == 1
    value = json.loads(output.read_text())
    assert value["passed"] is False
    assert value["maximum_channel_difference"] == 4
