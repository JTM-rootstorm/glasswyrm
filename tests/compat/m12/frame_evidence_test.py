#!/usr/bin/env python3
"""Focused stability and exact-equivalence validator coverage."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import tempfile


def load(name: str):
    path = pathlib.Path(__file__).with_name(name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ppm(pixel: bytes) -> bytes:
    return b"P6\n1 1\n255\n" + pixel


def main() -> int:
    capture = load("capture_stable_frame")
    compare = load("validate_frame_equivalence")
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-frames-") as temporary:
        root = pathlib.Path(temporary)
        dumps = root / "dumps"
        dumps.mkdir()
        (dumps / "frame-1.ppm").write_bytes(ppm(b"\x12\x34\x56"))
        for renderer in ("software", "gles"):
            frame = root / f"milestone12-{renderer}-testsprite.ppm"
            evidence = capture.capture(dumps, frame, 0.05)
            (root / f"milestone12-{renderer}-testsprite-stability.json").write_text(
                json.dumps(evidence) + "\n"
            )
            for scene in compare.SCENES[:-1]:
                (root / f"milestone12-{renderer}-{scene}.ppm").write_bytes(
                    ppm(scene.encode()[:3].ljust(3, b"!"))
                )
        if compare.validate(root).get("passed") is not True:
            return 1
        (root / "milestone12-gles-cursor.ppm").write_bytes(ppm(b"bad"))
        try:
            compare.validate(root)
        except ValueError:
            pass
        else:
            return 1

        generation_dumps = root / "generation-dumps"
        (generation_dumps / "2").mkdir(parents=True)
        (generation_dumps / "10").mkdir()
        (generation_dumps / "2" / "frame-999999.ppm").write_bytes(
            ppm(b"old")
        )
        current = generation_dumps / "10" / "frame-000001.ppm"
        current.write_bytes(ppm(b"new"))
        if capture.latest(generation_dumps) != current:
            raise RuntimeError(
                "stable-frame selection ignored the latest compositor generation"
            )
    print("M12 frame evidence tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
