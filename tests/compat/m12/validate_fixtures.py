#!/usr/bin/env python3
"""Validate the checksum-protected M12 probe contract."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys


BOOTSTRAP_FILES = {"README.md", "result-contract.json"}
M12_FILES = {
    "registry-little.json",
    "registry-big.json",
    "extensions.json",
    "testdraw2.trace.json",
    "testsprite2.trace.json",
    "sdl-probe.json",
    "testsprite2-software.ppm",
    "testsprite2-gles.ppm",
    "fullscreen-software.ppm",
    "fullscreen-gles.ppm",
    "renderer-software.jsonl",
    "renderer-gles.jsonl",
    "drm-damage-report.jsonl",
}


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) == 2 else pathlib.Path(__file__).resolve().parents[2] / "fixtures" / "m12"
    sums = root / "SHA256SUMS"
    if not sums.is_file():
        print("m12 fixtures: SHA256SUMS is missing", file=sys.stderr)
        return 1
    expected_files: set[str] = set()
    for line in sums.read_text().splitlines():
        expected, separator, name = line.partition("  ")
        candidate = pathlib.PurePosixPath(name)
        if not separator or candidate.is_absolute() or ".." in candidate.parts:
            print("m12 fixtures: unsafe checksum entry", file=sys.stderr)
            return 1
        path = root / candidate
        if not path.is_file() or digest(path) != expected:
            print(f"m12 fixtures: checksum mismatch for {name}", file=sys.stderr)
            return 1
        expected_files.add(name)
    complete = bool(expected_files & M12_FILES)
    expected_inventory = BOOTSTRAP_FILES | M12_FILES if complete else BOOTSTRAP_FILES
    if expected_files != expected_inventory:
        print("m12 fixtures: checksum inventory differs", file=sys.stderr)
        return 1
    contract = json.loads((root / "result-contract.json").read_text())
    if contract.get("schema") != 1:
        print("m12 fixtures: contract schema differs", file=sys.stderr)
        return 1
    if contract.get("profiles") != ["shm", "no-shm"]:
        print("m12 fixtures: profile order differs", file=sys.stderr)
        return 1
    if contract.get("raw_byte_orders") != ["little", "big"]:
        print("m12 fixtures: byte-order matrix differs", file=sys.stderr)
        return 1
    if set(contract.get("official_programs", ())) != {"testdraw2", "testsprite2"}:
        print("m12 fixtures: official workload set differs", file=sys.stderr)
        return 1
    if complete:
        for order in ("little", "big"):
            registry = json.loads((root / f"registry-{order}.json").read_text())
            if (
                registry.get("schema") != 1
                or registry.get("probe") != "m12_raw_probe"
                or registry.get("scenario") != "registry"
                or registry.get("byte_order") != order
                or registry.get("passed") is not True
                or not registry.get("checks")
                or not all(registry["checks"].values())
            ):
                print(f"m12 fixtures: {order} registry differs", file=sys.stderr)
                return 1
        extensions = json.loads((root / "extensions.json").read_text())
        if (
            extensions.get("schema") != 1
            or extensions.get("passed") is not True
            or extensions.get("evidence_errors") != []
            or set(extensions.get("profiles", {})) != {"shm", "no-shm"}
        ):
            print("m12 fixtures: normalized extension trace differs", file=sys.stderr)
            return 1
        for workload in ("testdraw2", "testsprite2"):
            trace = json.loads((root / f"{workload}.trace.json").read_text())
            if (
                trace.get("schema") != 1
                or trace.get("workload") != workload
                or trace.get("passed") is not True
                or trace.get("unexpected_errors") != []
                or not trace.get("recurring_image_classes")
            ):
                print(f"m12 fixtures: {workload} trace differs", file=sys.stderr)
                return 1
        sdl = json.loads((root / "sdl-probe.json").read_text())
        if (
            sdl.get("schema") != 1
            or sdl.get("probe") != "m12_sdl_probe"
            or sdl.get("passed") is not True
            or sdl.get("video_driver") != "x11"
            or sdl.get("display_count") != 1
            or not sdl.get("checks")
            or not all(sdl["checks"].values())
        ):
            print("m12 fixtures: SDL public probe differs", file=sys.stderr)
            return 1
        for name in (
            "testsprite2-software.ppm", "testsprite2-gles.ppm",
            "fullscreen-software.ppm", "fullscreen-gles.ppm",
        ):
            if not (root / name).read_bytes().startswith(b"P6\n"):
                print(f"m12 fixtures: {name} is not a binary PPM", file=sys.stderr)
                return 1
        if (root / "testsprite2-software.ppm").read_bytes() != \
           (root / "testsprite2-gles.ppm").read_bytes():
            print("m12 fixtures: testsprite2 renderer frames differ", file=sys.stderr)
            return 1
        if (root / "fullscreen-software.ppm").read_bytes() != \
           (root / "fullscreen-gles.ppm").read_bytes():
            print("m12 fixtures: fullscreen renderer frames differ", file=sys.stderr)
            return 1
        for name in ("renderer-software.jsonl", "renderer-gles.jsonl",
                     "drm-damage-report.jsonl"):
            if not (root / name).read_text(errors="strict").strip():
                print(f"m12 fixtures: {name} is empty", file=sys.stderr)
                return 1
    print("m12 fixtures: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
