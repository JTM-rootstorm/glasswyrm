#!/usr/bin/env python3
"""Regressions for binding M14 provenance to the executed files."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "gw-hw.d"))

from common import BUILD_PROVENANCE_SCHEMA, HarnessError  # noqa: E402
from live_runner import FixedLiveRunner, LIVE_UNITS  # noqa: E402
from provenance import (  # noqa: E402
    remove_staged_executables, stage_build_provenance,
)


TESTED_COMMIT = "b" * 40
CONFIG = {"tty": "/dev/tty2", "alternate_tty": "/dev/tty1"}


def write_manifest(
        build_root: Path, binaries: dict[str, Path],
        corrupt_role: str | None = None) -> None:
    records = []
    for role, path in binaries.items():
        contents = path.read_bytes()
        digest = hashlib.sha256(contents).hexdigest()
        if role == corrupt_role:
            digest = "0" * 64
        records.append({
            "role": role,
            "path": path.relative_to(build_root).as_posix(),
            "size": len(contents),
            "sha256": digest,
        })
    (build_root / "manifest.json").write_text(
        json.dumps({
            "schema": BUILD_PROVENANCE_SCHEMA,
            "source_commit": TESTED_COMMIT,
            "tracked_source_clean": True,
            "binaries": records,
        }) + "\n",
        encoding="utf-8",
    )


def make_binaries(build_root: Path) -> dict[str, Path]:
    binaries = {
        "libgwipc": build_root / "src/libgwipc.so.0.9.0",
        "gwcomp": build_root / "gwcomp",
        "gwinfo": build_root / "gwinfo",
    }
    for role, path in binaries.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((f"verified-{role}\n").encode("ascii"))
        path.chmod(0o700)
    return binaries


def expect_failure(action: object, text: str) -> None:
    try:
        action()  # type: ignore[operator]
    except HarnessError as error:
        assert text in str(error), str(error)
    else:
        raise AssertionError("unsafe provenance staging was accepted")


def test_staged_path_survives_source_replacement(root: Path) -> None:
    root.mkdir(mode=0o700)
    build_root = root / "build"
    build_root.mkdir()
    binaries = make_binaries(build_root)
    write_manifest(build_root, binaries)
    stage = root / "verified-bin"
    staged = stage_build_provenance(
        TESTED_COMMIT, stage, binaries=binaries, build_root=build_root,
        manifest_name="manifest.json",
    )

    expected = staged["gwcomp"].read_bytes()
    replacement = build_root / "replacement"
    replacement.write_bytes(b"unverified replacement\n")
    replacement.chmod(0o700)
    os.replace(replacement, binaries["gwcomp"])
    assert staged["gwcomp"].read_bytes() == expected
    assert staged["gwcomp"].stat().st_mode & 0o777 == 0o500
    assert stage.stat().st_mode & 0o077 == 0
    alias = staged["libgwipc"].parent / "libgwipc.so.0"
    assert alias.is_symlink()
    assert alias.readlink() == Path("libgwipc.so.0.9.0")
    original_library = staged["libgwipc"].read_bytes()
    binaries["libgwipc"].write_bytes(b"unverified library replacement\n")
    assert staged["libgwipc"].read_bytes() == original_library

    calls: list[list[str]] = []

    def execute(argv: list[str], _output: Path | None) -> int:
        calls.append(argv)
        return 0

    fixed = {
        "gwcomp": staged["gwcomp"],
        "systemd-run": Path("/usr/bin/systemd-run"),
    }
    runner = FixedLiveRunner(
        CONFIG, root, execute=execute, verify_paths=False,
        validate_runtime=False, fixed_binaries=fixed,
    )
    runner.start_unit(LIVE_UNITS["gwcomp"], "gwcomp", ["--backend", "drm"])
    separator = calls[0].index("--")
    assert calls[0][separator + 1] == str(staged["gwcomp"])
    assert calls[0][separator + 1] != str(binaries["gwcomp"])

    remove_staged_executables(stage, staged)
    assert not stage.exists()


def test_digest_mismatch_removes_partial_stage(root: Path) -> None:
    build_root = root / "bad-build"
    build_root.mkdir()
    binaries = make_binaries(build_root)
    write_manifest(build_root, binaries, corrupt_role="gwinfo")
    stage = root / "bad-stage"
    expect_failure(
        lambda: stage_build_provenance(
            TESTED_COMMIT, stage, binaries=binaries,
            build_root=build_root, manifest_name="manifest.json",
        ),
        "does not match provenance",
    )
    assert not stage.exists()


def test_symlink_source_is_rejected(root: Path) -> None:
    build_root = root / "symlink-build"
    build_root.mkdir()
    real = build_root / "real"
    real.write_bytes(b"real executable\n")
    real.chmod(0o700)
    linked = build_root / "gwcomp"
    linked.symlink_to(real.name)
    binaries = {"gwcomp": linked}
    write_manifest(build_root, binaries)
    stage = root / "symlink-stage"
    expect_failure(
        lambda: stage_build_provenance(
            TESTED_COMMIT, stage, binaries=binaries,
            build_root=build_root, manifest_name="manifest.json",
        ),
        "unavailable",
    )
    assert not stage.exists()


def test_early_failure_removes_stage_with_uncreated_nested_parents(
        root: Path) -> None:
    build_root = root / "early-build"
    build_root.mkdir()
    real = build_root / "real"
    real.write_bytes(b"real executable\n")
    real.chmod(0o700)
    linked = build_root / "gwcomp"
    linked.symlink_to(real.name)
    nested = build_root / "tests/manifest/m14/client"
    nested.parent.mkdir(parents=True)
    nested.write_bytes(b"verified client\n")
    nested.chmod(0o700)
    binaries = {"gwcomp": linked, "client": nested}
    write_manifest(build_root, binaries)
    stage = root / "early-stage"
    expect_failure(
        lambda: stage_build_provenance(
            TESTED_COMMIT, stage, binaries=binaries,
            build_root=build_root, manifest_name="manifest.json",
        ),
        "unavailable",
    )
    assert not stage.exists()


def test_non_private_staging_parent_is_rejected(root: Path) -> None:
    root.chmod(0o755)
    build_root = root / "build"
    build_root.mkdir()
    binaries = make_binaries(build_root)
    write_manifest(build_root, binaries)
    stage = root / "verified-bin"
    expect_failure(
        lambda: stage_build_provenance(
            TESTED_COMMIT, stage, binaries=binaries,
            build_root=build_root, manifest_name="manifest.json",
        ),
        "must be owned and private",
    )
    assert not stage.exists()


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        test_staged_path_survives_source_replacement(root / "stable")
        mismatch = root / "mismatch"
        mismatch.mkdir(mode=0o700)
        test_digest_mismatch_removes_partial_stage(mismatch)
        symlink = root / "symlink"
        symlink.mkdir(mode=0o700)
        test_symlink_source_is_rejected(symlink)
        early = root / "early"
        early.mkdir(mode=0o700)
        test_early_failure_removes_stage_with_uncreated_nested_parents(early)
        public_parent = root / "public-parent"
        public_parent.mkdir(mode=0o700)
        test_non_private_staging_parent_is_rejected(public_parent)
    print("M14 provenance execution binding: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
