#!/usr/bin/env python3
"""Validate the four exact software/GLES M12 opaque scene pairs."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib


SCENES = ("sdl-probe", "fullscreen", "cursor", "testsprite")


def digest(path: pathlib.Path) -> str:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise ValueError(f"{path}: scene is not binary PPM")
    return hashlib.sha256(data).hexdigest()


def stable_observation(path: pathlib.Path, expected_hash: str) -> dict:
    value = json.loads(path.read_text(errors="strict"))
    observations = value.get("observations")
    if (
        value.get("schema") != 1
        or value.get("stable") is not True
        or not isinstance(observations, list)
        or len(observations) != 2
        or {item.get("sha256") for item in observations} != {expected_hash}
        or value.get("output_sha256") != expected_hash
    ):
        raise ValueError(f"{path}: stable observation does not match sprite frame")
    return value


def validate(directory: pathlib.Path) -> dict:
    scenes = {}
    for scene in SCENES:
        software = directory / f"milestone12-software-{scene}.ppm"
        gles = directory / f"milestone12-gles-{scene}.ppm"
        software_hash = digest(software)
        gles_hash = digest(gles)
        if software.read_bytes() != gles.read_bytes():
            raise ValueError(f"{scene}: software and GLES frames differ")
        scenes[scene] = {
            "software_sha256": software_hash,
            "gles_sha256": gles_hash,
            "exact": True,
        }
    stable = {
        renderer: stable_observation(
            directory / f"milestone12-{renderer}-testsprite-stability.json",
            scenes["testsprite"][f"{renderer}_sha256"],
        )
        for renderer in ("software", "gles")
    }
    return {
        "schema": 1,
        "scenes": scenes,
        "stable_testsprite": stable,
        "passed": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        result = validate(arguments.artifact_dir)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"M12 frame equivalence: {error}")
        return 1
    arguments.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("M12 frame equivalence: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
