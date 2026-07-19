#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(validator: Path, fixtures: Path, expected: int) -> None:
    result = subprocess.run(
        [str(validator), str(fixtures)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"validator returned {result.returncode}, expected {expected}: "
            f"{result.stdout}{result.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("validator", type=Path)
    parser.add_argument("fixtures", type=Path)
    options = parser.parse_args()
    run(options.validator, options.fixtures, 0)
    with tempfile.TemporaryDirectory() as directory:
        copy = Path(directory) / "m14"
        shutil.copytree(options.fixtures, copy)
        fixture = copy / "gw-vrr-big.json"
        fixture.write_text(fixture.read_text(encoding="utf-8") + " ",
                           encoding="utf-8")
        run(options.validator, copy, 1)
    print("m14 host fixture validator test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
