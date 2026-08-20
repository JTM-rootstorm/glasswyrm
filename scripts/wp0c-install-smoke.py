#!/usr/bin/env python3
"""Stage retained Meson output and namespaced Rust transition candidates."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(message)


def inside_root(value: str, *, must_exist: bool = False) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if must_exist:
        resolved = path.resolve(strict=True)
    else:
        parent = path.parent.resolve(strict=True)
        resolved = parent / path.name
    if resolved != ROOT and ROOT not in resolved.parents:
        fail(f"path escapes Glasswyrm workspace: {value}")
    return resolved


def run(args: list[str], *, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def version_tuple(text: str) -> tuple[int, int, int]:
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
    if not match:
        fail(f"could not parse version: {text}")
    return tuple(int(value or 0) for value in match.groups())


def file_record(path: Path, root: Path) -> dict[str, str]:
    return {
        "path": path.relative_to(root).as_posix(),
        "sha256": sha256(path),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy-build", required=True)
    parser.add_argument("--destdir", required=True)
    parser.add_argument("--work-root", required=True)
    options = parser.parse_args()

    legacy = inside_root(options.legacy_build, must_exist=True)
    destdir = inside_root(options.destdir)
    work = inside_root(options.work_root)
    if not (legacy / "build.ninja").is_file():
        fail("--legacy-build is not a configured Meson build")
    for fresh in (destdir, work):
        if fresh.exists():
            fail(f"smoke path must not already exist: {fresh.relative_to(ROOT)}")
        fresh.mkdir(parents=True)

    build_options = json.loads(run(["meson", "introspect", "--buildoptions", str(legacy)]))
    values = {item["name"]: item.get("value") for item in build_options}
    if values.get("prefix") != "/usr" or values.get("libdir") != "lib64":
        fail("legacy build must use prefix=/usr and libdir=lib64")

    with (ROOT / "Cargo.toml").open("rb") as source:
        cargo_toml = tomllib.load(source)
    with (ROOT / "rust-toolchain.toml").open("rb") as source:
        toolchain = tomllib.load(source)
    minimum = cargo_toml["workspace"]["package"]["rust-version"]
    pin = toolchain["toolchain"]["channel"]
    rustc = run(["rustc", "--version"]).strip()
    if version_tuple(rustc) < version_tuple(minimum):
        fail(f"active rustc {rustc} is older than declared minimum {minimum}")

    env = os.environ.copy()
    env["CARGO_NET_OFFLINE"] = "true"
    env["CARGO_TARGET_DIR"] = str(work / "cargo-target")
    env.pop("GW_ALLOW_HARDWARE_TESTS", None)
    run(
        [
            "cargo", "build", "--locked", "--offline",
            "-p", "gwm", "--bin", "gwm", "-p", "gw-ipc-capi",
        ],
        env=env,
    )
    run(
        [
            str(ROOT / "crates/gw-ipc-capi/tests/run_smoke.sh"),
            str(ROOT), str(work / "cargo-target"), str(work / "capi-smoke"),
        ],
        env=env,
    )

    run(
        ["meson", "install", "-C", str(legacy), "--no-rebuild", "--destdir", str(destdir)]
    )
    legacy_paths = {
        path.relative_to(destdir).as_posix()
        for path in destdir.rglob("*")
        if path.is_file() or path.is_symlink()
    }

    transition = {
        "usr/libexec/glasswyrm-rust-transition/gwm": work / "cargo-target/debug/gwm",
        "usr/lib64/glasswyrm-rust-transition/libgw_ipc_capi.so": work / "cargo-target/debug/libgw_ipc_capi.so",
        "usr/lib64/glasswyrm-rust-transition/libgw_ipc_capi.a": work / "cargo-target/debug/libgw_ipc_capi.a",
        "usr/lib64/glasswyrm-rust-transition/libgwipc-transition.so": work / "capi-smoke/libgwipc-capi-smoke.so",
    }
    for relative, source in transition.items():
        if relative in legacy_paths:
            fail(f"unexpected transition install collision: {relative}")
        if not source.is_file():
            fail(f"missing Rust smoke artifact: {source.relative_to(ROOT)}")
        target = destdir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    public_headers = sorted(
        path.relative_to(destdir).as_posix()
        for path in (destdir / "usr/include/glasswyrm").rglob("ipc*")
        if path.is_file()
    )
    public_headers += sorted(
        path.relative_to(destdir).as_posix()
        for path in (destdir / "usr/include/glasswyrm/ipc").rglob("*")
        if path.is_file()
    )
    native_libraries = sorted(
        path.relative_to(destdir).as_posix()
        for path in (destdir / "usr/lib64").glob("libgwipc.so*")
    )
    pc = destdir / "usr/lib64/pkgconfig/gwipc.pc"
    native_real = next(
        (destdir / path for path in native_libraries if path.endswith(".0.9.0")),
        None,
    )
    if native_real is None or not pc.is_file() or not public_headers:
        fail("retained GWIPC install surface is incomplete")
    dynamic = run(["readelf", "-d", str(native_real)])
    if "Library soname: [libgwipc.so.0]" not in dynamic:
        fail("retained GWIPC SONAME is not libgwipc.so.0")

    lock_sources = [
        line.split("=", 1)[1].strip().strip('"')
        for line in (ROOT / "Cargo.lock").read_text().splitlines()
        if line.startswith("source = ")
    ]
    revision = run(["git", "rev-parse", "HEAD"]).strip()
    dirty = bool(run(["git", "status", "--porcelain"]).strip())
    transition_records = [file_record(destdir / path, destdir) for path in transition]
    manifest = {
        "schema": "glasswyrm-rust-transition-wp0c-install-v1",
        "revision": revision,
        "dirty": dirty,
        "toolchain": {
            "edition": cargo_toml["workspace"]["package"]["edition"],
            "minimum_rust": minimum,
            "tracked_pin": pin,
            "active_rustc": rustc,
            "active_matches_pin": version_tuple(rustc) == version_tuple(pin),
        },
        "cargo": {
            "locked": True,
            "offline": True,
            "lock_external_sources": lock_sources,
        },
        "legacy_install": {
            "prefix": values["prefix"],
            "libdir": values["libdir"],
            "path_count": len(legacy_paths),
            "gwipc_soname": "libgwipc.so.0",
            "gwipc_libraries": native_libraries,
            "gwipc_pkgconfig": pc.relative_to(destdir).as_posix(),
            "gwipc_headers": sorted(set(public_headers)),
        },
        "rust_transition_install": transition_records,
        "planned_cutover_collisions": {
            "executable": ["usr/bin/gwm"] if "usr/bin/gwm" in legacy_paths else [],
            "public_abi_libraries": native_libraries,
            "public_abi_headers": sorted(set(public_headers)),
            "public_abi_pkgconfig": [pc.relative_to(destdir).as_posix()],
            "disposition": "preserve native GWIPC 0.9 until the Rust C ABI covers the full installed contract",
        },
    }
    manifest_path = work / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(manifest_path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
