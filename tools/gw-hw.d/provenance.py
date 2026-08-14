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
PROBE_MANIFEST_NAME = "glasswyrm-m14-nvidia-probe-build-manifest.json"
PROBE_BUILD_ROOT = Path("/var/tmp/glasswyrm-build-m14-nvidia-probe")
PROBE_BINARY = PROBE_BUILD_ROOT / "tools/gw_drm_vrr_probe"
PROBE_ARTIFACT = "milestone14-nvidia-vrr-probe-build-provenance.json"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
PROVENANCE_BINARIES = {
    "libgwipc": BUILD_ROOT / "src/libgwipc.so.0.9.0",
    "gwm": BUILD_ROOT / "src/gwm",
    "gwcomp": BUILD_ROOT / "src/gwcomp",
    "server": BUILD_ROOT / "src/glasswyrmd",
    "gwout": BUILD_ROOT / "tools/gwout",
    "gwinfo": BUILD_ROOT / "tools/gwinfo",
    "client": BUILD_ROOT / "tests/manifest/m14/m14_vrr_client",
    "drm-probe": BUILD_ROOT / "tools/gw_drm_probe",
    "drm-vrr-probe": BUILD_ROOT / "tools/gw_drm_vrr_probe",
}


def _validate_document(
        manifest: dict[str, Any], tested_commit: str,
        binaries: dict[str, Path] = PROVENANCE_BINARIES,
        build_root: Path = BUILD_ROOT) -> None:
    if set(manifest) != {
            "schema", "source_commit", "tracked_source_clean", "binaries"}:
        raise HarnessError("build provenance manifest has a non-exact schema")
    if (manifest.get("schema") != BUILD_PROVENANCE_SCHEMA or
            manifest.get("source_commit") != tested_commit or
            manifest.get("tracked_source_clean") is not True):
        raise HarnessError(
            "build provenance does not identify the exact clean tested commit")
    records = manifest.get("binaries")
    if not isinstance(records, list) or len(records) != len(binaries):
        raise HarnessError("build provenance has the wrong executable set")
    expected_paths = {
        role: path.relative_to(build_root).as_posix()
        for role, path in binaries.items()
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
    if observed != set(binaries):
        raise HarnessError("build provenance executable roles are incomplete")


def validate_archived_provenance(
        path: Path, tested_commit: str,
        binaries: dict[str, Path] = PROVENANCE_BINARIES,
        build_root: Path = BUILD_ROOT) -> dict[str, Any]:
    if not COMMIT_PATTERN.fullmatch(tested_commit):
        raise HarnessError("tested commit is invalid for build provenance")
    manifest = _read_json(path)
    _validate_document(manifest, tested_commit, binaries, build_root)
    return manifest


def validate_probe_build_provenance(
        tested_commit: str, artifact_dir: Path | None = None) -> dict[str, Any]:
    binaries = {"drm-vrr-probe": PROBE_BINARY}
    try:
        status = PROBE_BUILD_ROOT.lstat()
    except OSError as error:
        raise HarnessError(
            "fixed NVIDIA probe build directory is unavailable") from error
    if (not stat.S_ISDIR(status.st_mode) or
            PROBE_BUILD_ROOT.resolve(strict=True) != PROBE_BUILD_ROOT):
        raise HarnessError(
            "fixed NVIDIA probe build directory must be a non-symlink directory")
    manifest = validate_archived_provenance(
        PROBE_BUILD_ROOT / PROBE_MANIFEST_NAME, tested_commit,
        binaries, PROBE_BUILD_ROOT)
    record = manifest["binaries"][0]
    size, digest = _hash_executable(PROBE_BINARY)
    if record["size"] != size or record["sha256"] != digest:
        raise HarnessError(
            "fixed NVIDIA VRR probe does not match provenance")
    if artifact_dir is not None:
        artifact_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        _write_json(artifact_dir / PROBE_ARTIFACT, manifest)
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


def _write_all(descriptor: int, contents: bytes) -> None:
    offset = 0
    while offset < len(contents):
        written = os.write(descriptor, contents[offset:])
        if written <= 0:
            raise OSError("short write while staging a verified executable")
        offset += written


def _stage_executable(
        source: Path, destination: Path,
        expected_size: int, expected_digest: str) -> None:
    """Copy and verify one executable through the same open source descriptor."""
    try:
        source_descriptor = os.open(
            source, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as error:
        raise HarnessError(
            f"fixed build executable is unavailable: {source}") from error
    destination_descriptor = -1
    try:
        status = os.fstat(source_descriptor)
        if (not stat.S_ISREG(status.st_mode) or status.st_size <= 0 or
                not status.st_mode & 0o111):
            raise HarnessError(
                f"fixed build executable is not a regular executable: {source}")
        destination_descriptor = os.open(
            destination,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC |
            os.O_NOFOLLOW,
            0o500,
        )
        digest = hashlib.sha256()
        copied = 0
        while True:
            chunk = os.read(source_descriptor, 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            _write_all(destination_descriptor, chunk)
            copied += len(chunk)
        os.fsync(destination_descriptor)
        if copied != expected_size or digest.hexdigest() != expected_digest:
            raise HarnessError(
                f"fixed build executable does not match provenance: "
                f"{destination.name}")
    finally:
        if destination_descriptor >= 0:
            os.close(destination_descriptor)
        os.close(source_descriptor)


def remove_staged_executables(
        directory: Path, binaries: dict[str, Path]) -> None:
    """Remove only the exact staged executable set and its private directory."""
    alias = directory / "src/libgwipc.so.0"
    alias.unlink(missing_ok=True)
    parents: set[Path] = set()
    for path in binaries.values():
        try:
            path.relative_to(directory)
        except ValueError as error:
            raise HarnessError(
                "staged executable escaped its fixed directory") from error
        parent = path.parent
        while parent != directory:
            parents.add(parent)
            parent = parent.parent
        if parent != directory:
            raise HarnessError("staged executable escaped its fixed directory")
        path.unlink(missing_ok=True)
    for parent in sorted(parents, key=lambda path: len(path.parts), reverse=True):
        try:
            parent.rmdir()
        except FileNotFoundError:
            pass
    directory.rmdir()


def stage_build_provenance(
        tested_commit: str, directory: Path,
        artifact_dir: Path | None = None,
        binaries: dict[str, Path] = PROVENANCE_BINARIES,
        build_root: Path = BUILD_ROOT,
        manifest_name: str = MANIFEST_NAME,
        artifact_name: str = BUILD_PROVENANCE_ARTIFACT) -> dict[str, Path]:
    """Bind provenance to private executable copies safe for deferred launch."""
    try:
        root_status = build_root.lstat()
    except OSError as error:
        raise HarnessError("fixed physical build directory is unavailable") from error
    if (not stat.S_ISDIR(root_status.st_mode) or
            build_root.resolve(strict=True) != build_root):
        raise HarnessError(
            "fixed physical build directory must be a non-symlink directory")
    manifest = validate_archived_provenance(
        build_root / manifest_name, tested_commit, binaries, build_root)
    records = {record["role"]: record for record in manifest["binaries"]}
    try:
        parent_status = directory.parent.lstat()
        if (not stat.S_ISDIR(parent_status.st_mode) or
                parent_status.st_uid != os.geteuid() or
                parent_status.st_mode & 0o077 or
                directory.parent.resolve(strict=True) != directory.parent):
            raise HarnessError(
                "verified executable staging parent must be owned and private")
        directory.mkdir(mode=0o700)
        directory_status = directory.lstat()
        if (not stat.S_ISDIR(directory_status.st_mode) or
                directory_status.st_mode & 0o077):
            raise HarnessError(
                "verified executable staging directory must be private")
    except OSError as error:
        raise HarnessError(
            "verified executable staging directory is unavailable") from error

    staged = {
        role: directory / str(records[role]["path"])
        for role in binaries
    }
    try:
        for role, source in binaries.items():
            record = records[role]
            staged[role].parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            _stage_executable(
                source, staged[role], record["size"], record["sha256"])
        if "libgwipc" in staged:
            alias = staged["libgwipc"].parent / "libgwipc.so.0"
            alias.symlink_to(staged["libgwipc"].name)
        if artifact_dir is not None:
            artifact_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
            _write_json(artifact_dir / artifact_name, manifest)
        return staged
    except Exception:
        try:
            remove_staged_executables(directory, staged)
        except OSError:
            pass
        raise


def stage_probe_build_provenance(
        tested_commit: str, directory: Path,
        artifact_dir: Path | None = None) -> dict[str, Path]:
    staged = stage_build_provenance(
        tested_commit, directory, artifact_dir,
        {"drm-vrr-probe": PROBE_BINARY}, PROBE_BUILD_ROOT,
        PROBE_MANIFEST_NAME, PROBE_ARTIFACT,
    )
    return {"nvidia-drm-vrr-probe": staged["drm-vrr-probe"]}


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
