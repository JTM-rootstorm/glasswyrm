"""Fail-closed validation for the fixed M14 physical build manifest."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import stat
from typing import Any

from common import (
    BUILD_PROVENANCE_ARTIFACT, BUILD_PROVENANCE_SCHEMA, BUILD_ROOT,
    COMMIT_PATTERN, HarnessError, _read_json, _write_json,
)


MANIFEST_NAME = "glasswyrm-m14-build-manifest.json"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
PROVENANCE_BINARIES = {
    "gwm": BUILD_ROOT / "src/gwm",
    "gwcomp": BUILD_ROOT / "src/gwcomp",
    "server": BUILD_ROOT / "src/glasswyrmd",
    "gwout": BUILD_ROOT / "tools/gwout",
    "gwinfo": BUILD_ROOT / "tools/gwinfo",
    "client": BUILD_ROOT / "tests/manifest/m14/m14_vrr_client",
    "drm-probe": BUILD_ROOT / "tools/gw_drm_probe",
}


def _validate_document(manifest: dict[str, Any], tested_commit: str) -> None:
    if set(manifest) != {
            "schema", "source_commit", "tracked_source_clean", "binaries"}:
        raise HarnessError("build provenance manifest has a non-exact schema")
    if (manifest.get("schema") != BUILD_PROVENANCE_SCHEMA or
            manifest.get("source_commit") != tested_commit or
            manifest.get("tracked_source_clean") is not True):
        raise HarnessError(
            "build provenance does not identify the exact clean tested commit")
    records = manifest.get("binaries")
    if not isinstance(records, list) or len(records) != len(PROVENANCE_BINARIES):
        raise HarnessError("build provenance has the wrong executable set")
    expected_paths = {
        role: path.relative_to(BUILD_ROOT).as_posix()
        for role, path in PROVENANCE_BINARIES.items()
    }
    observed: set[str] = set()
    for record in records:
        if not isinstance(record, dict) or set(record) != {
                "role", "path", "size", "sha256"}:
            raise HarnessError("build provenance executable record is malformed")
        role = record.get("role")
        size = record.get("size")
        digest = record.get("sha256")
        if (not isinstance(role, str) or role not in expected_paths or
                role in observed or record.get("path") != expected_paths[role] or
                isinstance(size, bool) or not isinstance(size, int) or size <= 0 or
                not isinstance(digest, str) or
                not SHA256_PATTERN.fullmatch(digest)):
            raise HarnessError("build provenance executable identity is invalid")
        observed.add(role)
    if observed != set(PROVENANCE_BINARIES):
        raise HarnessError("build provenance executable roles are incomplete")


def validate_archived_provenance(path: Path, tested_commit: str) -> dict[str, Any]:
    if not COMMIT_PATTERN.fullmatch(tested_commit):
        raise HarnessError("tested commit is invalid for build provenance")
    manifest = _read_json(path)
    _validate_document(manifest, tested_commit)
    return manifest


def _hash_executable(path: Path) -> tuple[int, str]:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as error:
        raise HarnessError(f"fixed build executable is unavailable: {path}") from error
    try:
        status = os.fstat(descriptor)
        if (not stat.S_ISREG(status.st_mode) or status.st_size <= 0 or
                not status.st_mode & 0o111):
            raise HarnessError(
                f"fixed build executable is not a regular executable: {path}")
        digest = hashlib.sha256()
        while True:
            chunk = os.read(descriptor, 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
        return status.st_size, digest.hexdigest()
    finally:
        os.close(descriptor)


def validate_build_provenance(
        tested_commit: str, artifact_dir: Path | None = None) -> dict[str, Any]:
    try:
        status = BUILD_ROOT.lstat()
    except OSError as error:
        raise HarnessError("fixed physical build directory is unavailable") from error
    if (not stat.S_ISDIR(status.st_mode) or
            BUILD_ROOT.resolve(strict=True) != BUILD_ROOT):
        raise HarnessError(
            "fixed physical build directory must be a non-symlink directory")
    manifest = validate_archived_provenance(
        BUILD_ROOT / MANIFEST_NAME, tested_commit)
    records = {record["role"]: record for record in manifest["binaries"]}
    for role, path in PROVENANCE_BINARIES.items():
        size, digest = _hash_executable(path)
        if (records[role]["size"] != size or
                records[role]["sha256"] != digest):
            raise HarnessError(
                f"fixed build executable does not match provenance: {role}")
    if artifact_dir is not None:
        artifact_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        _write_json(artifact_dir / BUILD_PROVENANCE_ARTIFACT, manifest)
    return manifest
