#!/usr/bin/env python3
"""Validate the checksum-protected M12 probe contract."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys


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
    if expected_files != {"README.md", "result-contract.json"}:
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
    print("m12 fixtures: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
