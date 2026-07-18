#!/usr/bin/env python3
"""Exercise accepted and rejected M13 fixture promotion archives."""

from __future__ import annotations

import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile


SOURCE_MAP = {
    "output-inventory.json": "milestone13-output-inventory.json",
    "layout-before.json": "milestone13-layout-before.json",
    "layout-after.json": "milestone13-layout-after.json",
    "randr-little.json": "milestone13-randr-little.json",
    "randr-big.json": "milestone13-randr-big.json",
    "gw-scale-little.json": "milestone13-gw-scale-little.json",
    "gw-scale-big.json": "milestone13-gw-scale-big.json",
    "legacy-left.ppm": "milestone13-legacy-left.ppm",
    "legacy-right.ppm": "milestone13-legacy-right.ppm",
    "legacy-spanning-left.ppm": "milestone13-legacy-spanning-left.ppm",
    "legacy-spanning-right.ppm": "milestone13-legacy-spanning-right.ppm",
    "aware-left.ppm": "milestone13-aware-left.ppm",
    "aware-right.ppm": "milestone13-aware-right.ppm",
    "rotate90.ppm": "milestone13-rotate90.ppm",
    "flipped.ppm": "milestone13-flipped.ppm",
    "frame-sets.jsonl": "milestone13-frame-sets.jsonl",
    "gwinfo-outputs.json": "milestone13-gwinfo-outputs.json",
    "gwinfo-windows.json": "milestone13-gwinfo-windows.json",
    "gwout-result.json": "milestone13-gwout-result.json",
    "scale-client-result.json": "milestone13-scale-client.json",
    "renderer-fractional-diff.json": "milestone13-renderer-fractional-diff.json",
}
COMMIT = "2" * 40


def ppm(width: int, height: int, seed: int) -> bytes:
    colors = [bytes(((seed + offset) % 255 + 1,
                     (seed * 3 + offset * 5) % 255 + 1,
                     (seed * 7 + offset * 11) % 255 + 1))
              for offset in range(4)]
    upper = colors[0] * (width // 2) + colors[1] * (width - width // 2)
    lower = colors[2] * (width // 2) + colors[3] * (width - width // 2)
    return (f"P6\n{width} {height}\n255\n".encode()
            + upper * (height // 2) + lower * (height - height // 2))


def write_sources(evidence: pathlib.Path) -> None:
    values = {
        "output-inventory.json": {"outputs": [
            {"id": "0000000000000001", "name": "LEFT"},
            {"id": "0000000000000002", "name": "RIGHT"}]},
        "layout-before.json": {"layout_generation": 3, "root_width": 1440,
                               "root_height": 600},
        "layout-after.json": {"layout_generation": 4, "root_width": 1280,
                              "root_height": 480},
        "gwinfo-outputs.json": {"layout_generation": 4, "outputs": [{}, {}]},
        "gwinfo-windows.json": {"layout_generation": 4, "windows": []},
        "gwout-result.json": {"result": 0, "applied_generation": 4},
        "renderer-fractional-diff.json": {
            "passed": True, "maximum_channel_difference": 1},
        "scale-client-result.json": {
            "schema": "glasswyrm.m13-scale-client.v1", "byte_order": "little",
            "logical_geometry": {"width": 320, "height": 240},
            "buffer_geometry": {"width": 640, "height": 480},
            "initial": {"primary": 1, "preferred": [1, 1], "memberships": [1]},
            "moved": {"primary": 2, "preferred": [2, 1], "memberships": [2]},
            "present_serial": 1, "reset_scale": 1},
    }
    for protocol in ("randr", "gw-scale"):
        for order in ("little", "big"):
            values[f"{protocol}-{order}.json"] = {
                "byte_order": order, "passed": True, "errors": []}
    for seed, (destination, source) in enumerate(SOURCE_MAP.items(), 1):
        path = evidence / source
        if destination.endswith(".ppm"):
            width, height = ((640, 480) if "left" in destination
                             else (800, 600))
            path.write_bytes(ppm(width, height, seed))
        elif destination == "frame-sets.jsonl":
            path.write_text(json.dumps({"generation": 4,
                "aggregate_hash": "0123456789abcdef",
                "outputs": [{"id": 1}, {"id": 2}]}) + "\n")
        else:
            path.write_text(json.dumps(values[destination]) + "\n")
    names = sorted(path.name for path in evidence.iterdir())
    (evidence / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256((evidence / name).read_bytes()).hexdigest()}  {name}\n"
        for name in names))


with tempfile.TemporaryDirectory() as name:
    root = pathlib.Path(name)
    artifacts = root / "artifacts"
    output = root / "fixtures"
    evidence = root / "evidence"
    artifacts.mkdir(); output.mkdir(); evidence.mkdir()
    shutil.copyfile(pathlib.Path(sys.argv[2]) / "scale-client-result.schema.json",
                    output / "scale-client-result.schema.json")
    (artifacts / "milestone13-summary.json").write_text(json.dumps({
        "passed": True,
        "required_base_commit": "d3440d3b8df1533410a9a2c4be46f2eea0cfb88d",
        "tested_commit": COMMIT, "evidence_errors": []}) + "\n")
    write_sources(evidence)
    with tarfile.open(artifacts / "milestone13-output-scaling-evidence.tar", "w") as archive:
        for path in evidence.iterdir():
            archive.add(path, arcname=path.name)
    command = [sys.argv[1], "--artifact-dir", str(artifacts),
               "--output-dir", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([sys.argv[3], output], check=True)

    summary = json.loads((artifacts / "milestone13-summary.json").read_text())
    summary["passed"] = False
    (artifacts / "milestone13-summary.json").write_text(json.dumps(summary))
    assert subprocess.run(command, check=False).returncode != 0
