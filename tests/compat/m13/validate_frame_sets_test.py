#!/usr/bin/env python3
"""Exercise the M13 atomic frame-set evidence validator."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import copy


def fnv(payload: bytes) -> int:
    value = 14695981039346656037
    for byte in payload:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def append(value: int, integer: int, width: int) -> int:
    for shift in range(0, width, 8):
        value = ((value ^ ((integer >> shift) & 0xFF)) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def write_ppm(path: pathlib.Path, width: int, height: int, pixel: bytes) -> str:
    payload = pixel * (width * height)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + payload)
    return f"{fnv(payload):016x}"


def aggregate(record: dict) -> str:
    value = 14695981039346656037
    for byte in b"glasswyrm-output-frame-set-v1":
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    value = append(value, record["layout_generation"], 64)
    value = append(value, int(record["primary_output_id"], 16), 64)
    transform = {"normal": 0, "flipped": 4}
    for output in record["outputs"]:
        value = append(value, int(output["output_id"], 16), 64)
        value = append(value, output["physical"]["width"], 32)
        value = append(value, output["physical"]["height"], 32)
        value = append(value, output["scale"]["numerator"], 32)
        value = append(value, output["scale"]["denominator"], 32)
        value = append(value, transform[output["transform"]], 32)
        value = append(value, int(output["fnv1a64"], 16), 64)
    return f"{value:016x}"


def main() -> int:
    validator = pathlib.Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        left_hash = write_ppm(root / "frame-left.ppm", 2, 1, b"\x10\x20\x30")
        right_hash = write_ppm(root / "frame-right.ppm", 1, 2, b"\x40\x50\x60")
        outputs = [
            {"output_id": "0000000000000011", "file": "frame-left.ppm",
             "fnv1a64": left_hash, "physical": {"width": 2, "height": 1},
             "logical": {"x": 0, "y": 0, "width": 2, "height": 1},
             "scale": {"numerator": 1, "denominator": 1},
             "transform": "normal",
             "damage": [{"x": 0, "y": 0, "width": 2, "height": 1}]},
            {"output_id": "0000000000000022", "file": "frame-right.ppm",
             "fnv1a64": right_hash, "physical": {"width": 1, "height": 2},
             "logical": {"x": 2, "y": 0, "width": 1, "height": 2},
             "scale": {"numerator": 1, "denominator": 1},
             "transform": "flipped", "damage": []},
        ]
        record = {"schema_version": 13, "transaction_ordinal": 7,
                  "commit_id": 4, "generation": 5, "layout_generation": 6,
                  "primary_output_id": "0000000000000011", "output_count": 2,
                  "outputs": outputs}
        record["aggregate_hash"] = aggregate(record)
        manifest = root / "frame-sets.jsonl"
        manifest.write_text(json.dumps(record) + "\n", encoding="utf-8")
        accepted = subprocess.run(
            [str(validator), str(manifest), str(root), "--print-last-aggregate"],
            check=True, capture_output=True, text=True)
        assert accepted.stdout.strip() == record["aggregate_hash"]

        def reject(changed: dict, message: str) -> None:
            changed["aggregate_hash"] = aggregate(changed)
            manifest.write_text(json.dumps(changed) + "\n", encoding="utf-8")
            rejected = subprocess.run(
                [str(validator), str(manifest), str(root)],
                capture_output=True, text=True)
            assert rejected.returncode != 0
            assert message in rejected.stderr, rejected.stderr

        changed = copy.deepcopy(record)
        changed["outputs"][0]["fnv1a64"] = "0000000000000000"
        reject(changed, "visible hash differs")

        changed = copy.deepcopy(record)
        changed["outputs"][0]["scale"] = {"numerator": 10,
                                                 "denominator": 8}
        reject(changed, "scale must be reduced")

        changed = copy.deepcopy(record)
        changed["outputs"][0]["scale"] = {"numerator": 121,
                                                 "denominator": 120}
        # 121/120 is reduced and exercises the accepted denominator boundary.
        changed["aggregate_hash"] = aggregate(changed)
        manifest.write_text(json.dumps(changed) + "\n", encoding="utf-8")
        subprocess.run([str(validator), str(manifest), str(root)], check=True)

        changed["outputs"][0]["scale"] = {"numerator": 122,
                                                 "denominator": 121}
        reject(changed, "denominator exceeds")

        changed = copy.deepcopy(record)
        changed["outputs"][0]["scale"] = {"numerator": 5,
                                                 "denominator": 1}
        reject(changed, "within 1/1 through 4/1")

        changed = copy.deepcopy(record)
        changed["outputs"] = changed["outputs"][:1]
        changed["output_count"] = 1
        changed["aggregate_hash"] = aggregate(changed)
        manifest.write_text(json.dumps(changed) + "\n", encoding="utf-8")
        subprocess.run([str(validator), str(manifest), str(root)], check=True)

        changed["output_count"] = 2
        reject(changed, "one through eight outputs")

        changed = copy.deepcopy(record)
        changed["outputs"].reverse()
        reject(changed, "stable ascending output-ID order")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
