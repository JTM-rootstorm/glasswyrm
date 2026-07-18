#!/usr/bin/env python3
"""Promote an accepted Milestone 13 evidence archive into reviewed fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile

import validate_fixtures


REQUIRED_BASE = "d3440d3b8df1533410a9a2c4be46f2eea0cfb88d"
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


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: pathlib.Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} is not a JSON object")
    return value


def verify_summary(path: pathlib.Path) -> dict:
    value = load_object(path)
    tested = value.get("tested_commit")
    if (value.get("passed") is not True
            or value.get("required_base_commit") != REQUIRED_BASE
            or value.get("evidence_errors") != []
            or not isinstance(tested, str) or len(tested) != 40
            or any(character not in "0123456789abcdef" for character in tested)):
        raise ValueError("summary is not an accepted Milestone 13 run")
    return value


def extract_evidence(archive: pathlib.Path, directory: pathlib.Path) -> None:
    with tarfile.open(archive, "r:") as source:
        members = source.getmembers()
        for member in members:
            name = pathlib.PurePosixPath(member.name.removeprefix("./"))
            if (name.is_absolute() or ".." in name.parts
                    or len(name.parts) != 1 or not member.isfile()):
                raise ValueError(f"unsafe evidence member: {member.name}")
            member.name = name.name
            source.extract(member, directory)
    sums = directory / "SHA256SUMS"
    if not sums.is_file():
        raise ValueError("evidence archive has no SHA256SUMS")
    listed: set[str] = set()
    for line in sums.read_text(encoding="ascii").splitlines():
        expected, separator, name = line.partition("  ")
        candidate = pathlib.PurePosixPath(name)
        if (not separator or len(candidate.parts) != 1
                or name in listed or not (directory / name).is_file()
                or sha256(directory / name) != expected):
            raise ValueError("evidence checksum inventory is invalid")
        listed.add(name)
    missing = set(SOURCE_MAP.values()) - listed
    if missing:
        raise ValueError("evidence archive lacks: " + ", ".join(sorted(missing)))


def readme(summary: dict) -> str:
    return f"""# Milestone 13 output and scaling fixtures

Status: accepted deterministic fixtures for the experimental Milestone 13
output-model and scaling profile.

These fixtures were promoted from the checksum-protected Gentoo VM evidence
archive captured at commit `{summary['tested_commit']}` on required base
`{REQUIRED_BASE}`. They cover the canonical two-output headless layout, raw
little- and big-endian RANDR/GW_SCALE probes, legacy and scale-aware surfaces,
all required transforms, output tools, and software/GLES fractional evidence.

Regenerate only from a newly accepted VM run, then review output geometry,
seams, transforms, and scale filtering before committing:

```sh
tests/compat/m13/promote_fixtures.py \\
  --artifact-dir artifacts/vm/latest \\
  --output-dir tests/fixtures/m13
tests/compat/m13/validate_fixtures.py \\
  --require-complete tests/fixtures/m13
```

Do not regenerate these fixtures in ordinary tests. The repository client is a
raw protocol proof and does not imply toolkit GW_SCALE integration.
"""


def promote(artifacts: pathlib.Path, output: pathlib.Path) -> None:
    summary = verify_summary(artifacts / "milestone13-summary.json")
    archive = artifacts / "milestone13-output-scaling-evidence.tar"
    schema = output / "scale-client-result.schema.json"
    if not archive.is_file() or not schema.is_file():
        raise ValueError("evidence archive or scale-client schema is missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix=".m13-evidence-", dir=output.parent) as evidence_name, \
         tempfile.TemporaryDirectory(
            prefix=".m13-fixtures-", dir=output.parent) as staging_name:
        evidence = pathlib.Path(evidence_name)
        staging = pathlib.Path(staging_name)
        extract_evidence(archive, evidence)
        for destination, source in SOURCE_MAP.items():
            shutil.copyfile(evidence / source, staging / destination)
        shutil.copyfile(schema, staging / schema.name)
        (staging / "README.md").write_text(readme(summary), encoding="utf-8")
        names = sorted(path.name for path in staging.iterdir() if path.is_file())
        (staging / "SHA256SUMS").write_text("".join(
            f"{sha256(staging / name)}  {name}\n" for name in names),
            encoding="ascii")
        validate_fixtures.validate(staging, require_complete=True)
        output.mkdir(parents=True, exist_ok=True)
        for path in output.iterdir():
            if path.is_file():
                path.unlink()
        for path in staging.iterdir():
            shutil.copyfile(path, output / path.name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    try:
        promote(arguments.artifact_dir.resolve(), arguments.output_dir.resolve())
    except (OSError, UnicodeError, json.JSONDecodeError, tarfile.TarError,
            ValueError) as error:
        print(f"M13 fixture promotion: {error}")
        return 1
    print("M13 fixture promotion: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
