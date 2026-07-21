#!/usr/bin/env python3
"""Exercise exact-commit generation and fail-closed M14 binary validation."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROLES = (
    ("gwm", "src/gwm"),
    ("gwcomp", "src/gwcomp"),
    ("server", "src/glasswyrmd"),
    ("gwout", "tools/gwout"),
    ("gwinfo", "tools/gwinfo"),
    ("client", "tests/manifest/m14/m14_vrr_client"),
    ("drm-probe", "tools/gw_drm_probe"),
)


def command(*arguments: object, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(argument) for argument in arguments],
        cwd=cwd,
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: m14_build_provenance_test.py GENERATOR MODULE_DIR")
    generator = Path(sys.argv[1]).resolve()
    module_dir = Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_dir))
    import provenance  # noqa: E402

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source"
        build = root / "build"
        source.mkdir()
        build.mkdir()
        tracked = source / "tracked.txt"
        tracked.write_text("accepted source\n", encoding="utf-8")
        assert command("git", "init", "-q", cwd=source).returncode == 0
        assert command("git", "add", "tracked.txt", cwd=source).returncode == 0
        committed = command(
            "git", "-c", "user.name=Provenance Test",
            "-c", "user.email=provenance@example.invalid",
            "-c", "commit.gpgsign=false",
            "commit", "-q", "-m", "test fixture", cwd=source)
        assert committed.returncode == 0, committed.stderr
        commit = command("git", "rev-parse", "HEAD", cwd=source).stdout.strip()

        binary_paths: list[Path] = []
        for index, (_, relative) in enumerate(ROLES):
            path = build / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"binary-{index}\n".encode("ascii"))
            path.chmod(0o755)
            binary_paths.append(path)
        output = build / provenance.MANIFEST_NAME
        git_program = shutil.which("git")
        assert git_program is not None
        generated = command(
            generator,
            "--source-root", source,
            "--git", git_program,
            "--expected-commit", commit,
            "--output", output,
            "--roles", ",".join(role for role, _ in ROLES),
            "--binaries", *binary_paths,
        )
        assert generated.returncode == 0, generated.stderr
        manifest = json.loads(output.read_text(encoding="utf-8"))
        assert manifest["source_commit"] == commit
        assert manifest["tracked_source_clean"] is True
        for record, path in zip(manifest["binaries"], binary_paths, strict=True):
            assert record["size"] == path.stat().st_size
            assert record["sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()

        provenance.BUILD_ROOT = build.resolve()
        provenance.PROVENANCE_BINARIES = {
            role: build / relative for role, relative in ROLES
        }
        artifacts = root / "artifacts"
        provenance.validate_build_provenance(commit, artifacts)
        assert (artifacts / "milestone14-build-provenance.json").is_file()
        binary_paths[0].write_bytes(b"changed after manifest\n")
        try:
            provenance.validate_build_provenance(commit)
        except provenance.HarnessError:
            pass
        else:
            raise AssertionError("post-manifest binary mutation was accepted")

        tracked.write_text("dirty source\n", encoding="utf-8")
        dirty_output = build / "dirty-manifest.json"
        dirty = command(
            generator,
            "--source-root", source,
            "--git", git_program,
            "--expected-commit", commit,
            "--output", dirty_output,
            "--roles", ",".join(role for role, _ in ROLES),
            "--binaries", *binary_paths,
        )
        assert dirty.returncode == 1
        assert "tracked source changes" in dirty.stderr
        assert not dirty_output.exists()

    print("M14 build provenance test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
