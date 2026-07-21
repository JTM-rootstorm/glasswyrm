#!/usr/bin/env python3
"""Generate the exact-binary manifest for an M14 physical validation build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys


SCHEMA = "glasswyrm.m14-build-provenance.v1"
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
EXPECTED_PATHS = {
    "gwm": "src/gwm",
    "gwcomp": "src/gwcomp",
    "server": "src/glasswyrmd",
    "gwout": "tools/gwout",
    "gwinfo": "tools/gwinfo",
    "client": "tests/manifest/m14/m14_vrr_client",
    "drm-probe": "tools/gw_drm_probe",
}


def fail(message: str) -> None:
    raise ValueError(message)


def git(git_program: Path, source_root: Path, *arguments: str) -> str:
    result = subprocess.run(
        [str(git_program), "-C", str(source_root), *arguments],
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        fail(f"Git source identity check failed: {result.stderr.strip()}")
    return result.stdout.strip()


def hash_regular_executable(path: Path) -> tuple[int, str]:
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode):
            fail(f"build input is not a regular file: {path}")
        if status.st_size <= 0 or not status.st_mode & 0o111:
            fail(f"build input is empty or non-executable: {path}")
        digest = hashlib.sha256()
        while True:
            chunk = os.read(descriptor, 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
        return status.st_size, digest.hexdigest()
    finally:
        os.close(descriptor)


def generate(source_root: Path, git_program: Path, expected_commit: str, output: Path,
             roles: list[str], binaries: list[Path]) -> None:
    if not COMMIT_PATTERN.fullmatch(expected_commit):
        fail("expected commit must be exactly 40 lowercase hexadecimal digits")
    source_root = source_root.resolve(strict=True)
    git_program = git_program.resolve(strict=True)
    repository_root = Path(git(
        git_program, source_root, "rev-parse", "--show-toplevel"))
    if repository_root.resolve(strict=True) != source_root:
        fail("Meson source root is not the Git repository root")
    if git(git_program, source_root, "rev-parse", "--verify", "HEAD") != expected_commit:
        fail("Git HEAD changed after the physical-validation build was configured")
    if git(git_program, source_root, "status", "--porcelain=v1", "--untracked-files=no"):
        fail("tracked source changes prevent exact physical-build provenance")
    if roles != list(EXPECTED_PATHS) or len(binaries) != len(roles):
        fail("physical build roles do not match the fixed M14 executable set")

    build_root = output.parent.resolve(strict=True)
    records: list[dict[str, object]] = []
    for role, path in zip(roles, binaries, strict=True):
        absolute = path.resolve(strict=True)
        try:
            relative = absolute.relative_to(build_root).as_posix()
        except ValueError:
            fail(f"{role} is outside the Meson build root")
        if relative != EXPECTED_PATHS[role]:
            fail(f"{role} has unexpected build path {relative}")
        size, digest = hash_regular_executable(path)
        records.append({
            "role": role,
            "path": relative,
            "size": size,
            "sha256": digest,
        })

    manifest = {
        "schema": SCHEMA,
        "source_commit": expected_commit,
        "tracked_source_clean": True,
        "binaries": records,
    }
    temporary = output.with_name(f".{output.name}.tmp")
    temporary.unlink(missing_ok=True)
    descriptor = os.open(
        temporary,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o600,
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--git", type=Path, required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--roles", required=True)
    parser.add_argument("--binaries", type=Path, nargs="+", required=True)
    options = parser.parse_args()
    try:
        generate(
            options.source_root,
            options.git,
            options.expected_commit,
            options.output,
            options.roles.split(","),
            options.binaries,
        )
        return 0
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        print(f"M14 physical build provenance failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
