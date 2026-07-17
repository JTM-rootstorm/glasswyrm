#!/usr/bin/env python3
"""Capture a live PPM only after two separated observations are identical."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import time


def latest(directory: pathlib.Path) -> pathlib.Path:
    candidates = sorted(directory.glob("**/*.ppm"), key=lambda path: path.name)
    if not candidates:
        raise ValueError("frame dump has no PPM")
    return candidates[-1]


def sample(directory: pathlib.Path) -> tuple[pathlib.Path, str]:
    path = latest(directory)
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise ValueError(f"{path}: frame is not binary PPM")
    return path, hashlib.sha256(data).hexdigest()


def capture(
    directory: pathlib.Path, output: pathlib.Path, interval: float,
    timeout: float = 10.0,
) -> dict:
    deadline = time.monotonic() + timeout
    first_path, first_hash = sample(directory)
    while True:
        time.sleep(interval)
        second_path, second_hash = sample(directory)
        if first_hash == second_hash:
            break
        if time.monotonic() >= deadline:
            raise ValueError("sprite frame did not become stable before timeout")
        first_path, first_hash = second_path, second_hash
    shutil.copyfile(second_path, output)
    return {
        "schema": 1,
        "stable": True,
        "interval_seconds": interval,
        "observations": [
            {"file": first_path.name, "sha256": first_hash},
            {"file": second_path.name, "sha256": second_hash},
        ],
        "output": output.name,
        "output_sha256": second_hash,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dump-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output-frame", type=pathlib.Path, required=True)
    parser.add_argument("--output-json", type=pathlib.Path, required=True)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    arguments = parser.parse_args()
    if arguments.interval < 0.05 or arguments.interval > 10:
        parser.error("--interval must be between 0.05 and 10 seconds")
    if arguments.timeout < arguments.interval or arguments.timeout > 60:
        parser.error("--timeout must be between the interval and 60 seconds")
    try:
        result = capture(
            arguments.dump_dir, arguments.output_frame, arguments.interval,
            arguments.timeout,
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(f"M12 stable frame: {error}")
        return 1
    arguments.output_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("M12 stable frame: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
