#!/usr/bin/env python3
"""Promote only a checksum-valid, accepted M12 VM run into fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile
from typing import Any

import validate_runtime_reports


REQUIRED_BASE = "ae6b6c93a29a1fb985dcea8455650d15c0fec364"
SOURCE_MAP = {
    "registry-little.json": "milestone12-raw-little-registry.json",
    "registry-big.json": "milestone12-raw-big-registry.json",
    "extensions.json": "milestone12-extension-trace.json",
    "testdraw2.trace.json": "milestone12-testdraw2-trace.json",
    "testsprite2.trace.json": "milestone12-testsprite2-trace.json",
    "sdl-probe.json": "milestone12-sdl-probe.json",
    "testsprite2-software.ppm": "milestone12-software-testsprite.ppm",
    "testsprite2-gles.ppm": "milestone12-gles-testsprite.ppm",
    "fullscreen-software.ppm": "milestone12-software-fullscreen.ppm",
    "fullscreen-gles.ppm": "milestone12-gles-fullscreen.ppm",
    "renderer-software.jsonl": "milestone12-renderer-software.jsonl",
    "renderer-gles.jsonl": "milestone12-renderer-gles.jsonl",
    "drm-damage-report.jsonl": "milestone12-drm-damage-report.jsonl",
}


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(errors="strict"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} is not a JSON object")
    return value


def validate_summary(path: pathlib.Path) -> dict[str, Any]:
    summary = load_object(path)
    tested = summary.get("tested_commit")
    if (
        summary.get("passed") is not True
        or summary.get("required_base_commit") != REQUIRED_BASE
        or not isinstance(tested, str)
        or len(tested) != 40
        or any(character not in "0123456789abcdef" for character in tested)
        or summary.get("evidence_errors") != []
        or summary.get("facts", {}).get("scenario_exit") != "0"
    ):
        raise ValueError("summary is not a complete accepted M12 run")
    return summary


def extract_verified(archive: pathlib.Path, destination: pathlib.Path) -> None:
    with tarfile.open(archive, "r:") as source:
        members = source.getmembers()
        for member in members:
            candidate = pathlib.PurePosixPath(member.name.removeprefix("./"))
            if (
                candidate.is_absolute()
                or ".." in candidate.parts
                or len(candidate.parts) != 1
                or not member.isfile()
            ):
                raise ValueError(f"unsafe evidence member: {member.name}")
            member.name = candidate.name
            source.extract(member, destination)
    sums = destination / "SHA256SUMS"
    if not sums.is_file():
        raise ValueError("evidence archive has no SHA256SUMS")
    listed: set[str] = set()
    for line in sums.read_text(errors="strict").splitlines():
        expected, separator, name = line.partition("  ")
        candidate = pathlib.PurePosixPath(name.removeprefix("./"))
        if not separator or len(candidate.parts) != 1 or candidate.name in listed:
            raise ValueError("evidence checksum inventory is malformed")
        path = destination / candidate.name
        if not path.is_file() or sha256(path) != expected:
            raise ValueError(f"evidence checksum mismatch for {candidate.name}")
        listed.add(candidate.name)
    required = set(SOURCE_MAP.values())
    if not required <= listed:
        missing = ", ".join(sorted(required - listed))
        raise ValueError(f"evidence archive lacks fixture sources: {missing}")


def validate_raw(path: pathlib.Path, order: str) -> None:
    value = load_object(path)
    checks = value.get("checks")
    evidence = value.get("evidence")
    if (
        value.get("schema") != 1
        or value.get("probe") != "m12_raw_probe"
        or value.get("scenario") != "registry"
        or value.get("byte_order") != order
        or value.get("passed") is not True
        or not isinstance(checks, dict)
        or not checks
        or not all(check is True for check in checks.values())
        or not isinstance(evidence, dict)
        or not isinstance(evidence.get("assignments"), dict)
        or not isinstance(evidence.get("list_extensions"), list)
    ):
        raise ValueError(f"{path.name} is not an accepted {order}-endian registry")


def validate_json_sources(root: pathlib.Path) -> None:
    validate_raw(root / SOURCE_MAP["registry-little.json"], "little")
    validate_raw(root / SOURCE_MAP["registry-big.json"], "big")
    extensions = load_object(root / SOURCE_MAP["extensions.json"])
    if (
        extensions.get("schema") != 1
        or extensions.get("passed") is not True
        or extensions.get("evidence_errors") != []
        or set(extensions.get("profiles", {})) != {"shm", "no-shm"}
    ):
        raise ValueError("extension trace summary is not accepted")
    for workload in ("testdraw2", "testsprite2"):
        trace = load_object(root / SOURCE_MAP[f"{workload}.trace.json"])
        if (
            trace.get("schema") != 1
            or trace.get("workload") != workload
            or trace.get("passed") is not True
            or trace.get("unexpected_errors") != []
            or not trace.get("recurring_image_classes")
        ):
            raise ValueError(f"{workload} normalized trace is not accepted")
    sdl = load_object(root / SOURCE_MAP["sdl-probe.json"])
    if (
        sdl.get("schema") != 1
        or sdl.get("probe") != "m12_sdl_probe"
        or sdl.get("passed") is not True
        or sdl.get("video_driver") != "x11"
        or sdl.get("display_count") != 1
        or not isinstance(sdl.get("checks"), dict)
        or not sdl["checks"]
        or not all(check is True for check in sdl["checks"].values())
    ):
        raise ValueError("SDL public probe result is not accepted")


def validate_ppms(root: pathlib.Path) -> None:
    for destination, source in SOURCE_MAP.items():
        if not destination.endswith(".ppm"):
            continue
        data = (root / source).read_bytes()
        if not data.startswith(b"P6\n") or b"\n255\n" not in data[:128]:
            raise ValueError(f"{source} is not a binary PPM")
    for stem in ("testsprite2", "fullscreen"):
        software = root / SOURCE_MAP[f"{stem}-software.ppm"]
        gles = root / SOURCE_MAP[f"{stem}-gles.ppm"]
        if software.read_bytes() != gles.read_bytes():
            raise ValueError(f"{stem} software/GLES evidence differs")


def validate_reports(root: pathlib.Path) -> None:
    validate_runtime_reports.validate_renderer(
        root / SOURCE_MAP["renderer-software.jsonl"], "software"
    )
    validate_runtime_reports.validate_renderer(
        root / SOURCE_MAP["renderer-gles.jsonl"], "gles"
    )
    validate_runtime_reports.validate_drm(
        root / SOURCE_MAP["drm-damage-report.jsonl"]
    )


def provenance_readme(summary: dict[str, Any]) -> str:
    facts = summary["facts"]
    return f"""# Milestone 12 compatibility fixtures

Status: accepted deterministic fixtures for the exact SDL 2.32.10 X11
software-renderer profile documented in `docs/compatibility/M12_SDL.md`.

These files were promoted from the checksum-protected full Gentoo VM evidence
archive captured at commit `{summary['tested_commit']}` on the required base
`{REQUIRED_BASE}`. The SDL source archive SHA-256 was
`{facts.get('sdl_source_sha256', 'unrecorded')}`.

`registry-little.json`, `registry-big.json`, and `sdl-probe.json` retain the
repository probe results. `extensions.json`, `testdraw2.trace.json`, and
`testsprite2.trace.json` are bounded normalized summaries; the raw JSONL trace
remains binary acceptance evidence and is deliberately not a golden fixture.
The PPM pairs are byte-identical opaque software/GLES outputs. Renderer and DRM
JSONL files retain the accepted runtime diagnostics used to validate damage,
synchronization, VT recovery, and restoration.

`result-contract.json` freezes the required probe/workload matrix.
`SHA256SUMS` protects every ordinary fixture and this provenance document.
Regenerate this directory only from a new fully accepted run:

```sh
tests/compat/m12/promote_fixtures.py \
  --artifact-dir artifacts/vm/latest \
  --output-dir tests/fixtures/m12
tests/compat/m12/validate_fixtures.py tests/fixtures/m12
```

Do not promote a failed run, hand-edit normalized traces, manufacture frames,
or replace raw runtime evidence with repository goldens.
"""


def promote(artifact_dir: pathlib.Path, output_dir: pathlib.Path,
            contract: pathlib.Path) -> None:
    summary = validate_summary(artifact_dir / "milestone12-summary.json")
    archive = artifact_dir / "milestone12-efficient-sdl-evidence.tar"
    if not archive.is_file():
        raise ValueError("accepted evidence archive is missing")
    contract_value = load_object(contract)
    if contract_value.get("schema") != 1:
        raise ValueError("result contract schema differs")
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".m12-fixture-evidence-", dir=output_dir.parent
    ) as evidence_name, tempfile.TemporaryDirectory(
        prefix=".m12-fixture-output-", dir=output_dir.parent
    ) as staging_name:
        evidence = pathlib.Path(evidence_name)
        staging = pathlib.Path(staging_name)
        extract_verified(archive, evidence)
        validate_json_sources(evidence)
        validate_ppms(evidence)
        validate_reports(evidence)
        for destination, source in SOURCE_MAP.items():
            shutil.copyfile(evidence / source, staging / destination)
        shutil.copyfile(contract, staging / "result-contract.json")
        (staging / "README.md").write_text(provenance_readme(summary))
        checksummed = sorted(path.name for path in staging.iterdir() if path.is_file())
        (staging / "SHA256SUMS").write_text("".join(
            f"{sha256(staging / name)}  {name}\n" for name in checksummed
        ))
        output_dir.mkdir(parents=True, exist_ok=True)
        for path in output_dir.iterdir():
            if path.is_file():
                path.unlink()
        for path in staging.iterdir():
            shutil.copyfile(path, output_dir / path.name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--contract", type=pathlib.Path)
    arguments = parser.parse_args()
    contract = arguments.contract or arguments.output_dir / "result-contract.json"
    try:
        promote(arguments.artifact_dir.resolve(), arguments.output_dir.resolve(),
                contract.resolve())
    except (OSError, UnicodeError, json.JSONDecodeError, tarfile.TarError,
            ValueError) as error:
        print(f"M12 fixture promotion: {error}")
        return 1
    print("M12 fixture promotion: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
