#!/usr/bin/env python3
"""Validate the frozen SDL 2.32.10 compatibility manifest and source tree."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys
import tomllib


SDL_ARCHIVE_SHA256 = "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
SDL_SIGNATURE_SHA256 = "9533de95863efb5f3fb47fd22adda9be8f1d438b928b1287cea433d4d2ef10ad"
SDL_SIGNING_FINGERPRINT = "1528635D8053A57F77D1E08630A59377A7763BE6"
PROGRAM_HASHES = {
    "test/testdraw2.c": "e2d67b758d974bd9e07a4dcbe0107fbcf9ed342382a24ed63024699c7459cd5f",
    "test/testsprite2.c": "34c32afde10a35f72b6ebe80ede177c26ff70f528c7f76949865072654101ea8",
    "test/icon.bmp": "f7b5cca4aabd94ba4cbaff14bde09ff1424403185e0613d48c77cc450064531e",
}
REQUIRED_BUILD_ARGUMENTS = {
    "-DSDL_SHARED=ON",
    "-DSDL_STATIC=OFF",
    "-DSDL_TEST=ON",
    "-DSDL_TESTS=OFF",
    "-DSDL_X11=ON",
    "-DSDL_X11_SHARED=ON",
    "-DSDL_X11_XFIXES=ON",
    "-DSDL_X11_XRANDR=ON",
    "-DSDL_X11_XCURSOR=OFF",
    "-DSDL_X11_XINPUT=OFF",
    "-DSDL_X11_XSHAPE=OFF",
    "-DSDL_WAYLAND=OFF",
    "-DSDL_KMSDRM=OFF",
    "-DSDL_OPENGL=OFF",
    "-DSDL_OPENGLES=OFF",
    "-DSDL_VULKAN=OFF",
    "-DSDL_JOYSTICK=OFF",
    "-DSDL_HAPTIC=OFF",
    "-DSDL_SENSOR=OFF",
}
REQUIRED_ENVIRONMENT = {
    "LC_ALL=C",
    "LANG=C",
    "XMODIFIERS=@im=none",
    "SESSION_MANAGER=",
    "XAUTHORITY=/dev/null",
    "DISPLAY=:99",
    "SDL_VIDEODRIVER=x11",
    "SDL_RENDER_DRIVER=software",
    "SDL_AUDIODRIVER=dummy",
    "LD_PRELOAD=tests/libgw_m9_fixed_time.so",
}
OFFICIAL_EXTENSION_QUERIES = ["BIG-REQUESTS", "OTHER", "RANDR", "MIT-SHM"]


def digest(path: pathlib.Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--archive", type=pathlib.Path)
    parser.add_argument("--signature", type=pathlib.Path)
    parser.add_argument("--source-tree", type=pathlib.Path)
    arguments = parser.parse_args()

    errors: list[str] = []
    try:
        raw = arguments.manifest.read_bytes()
        document = tomllib.loads(raw.decode("utf-8"))
    except (OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        print(f"manifest cannot be read: {error}", file=sys.stderr)
        return 2

    require(b"PENDING" not in raw.upper(), "manifest contains PENDING", errors)
    require(document.get("schema") == 1, "schema must be 1", errors)
    require(
        document.get("required_base") == "ae6b6c93a29a1fb985dcea8455650d15c0fec364",
        "required_base is not the frozen M12 base",
        errors,
    )
    require(
        document.get("fixture_contract") == "tests/fixtures/m12/result-contract.json"
        and document.get("fixture_contract_sha256")
        == "684f9fd7b8ddb0813e75070df52132c69732101f5c05d68faa869d4fc9de2e6f",
        "fixture contract identity differs",
        errors,
    )
    require(
        set(document.get("expected_frame_artifacts", ()))
        == {
            "milestone12-software.ppm", "milestone12-gles.ppm",
            "milestone12-fullscreen.ppm", "milestone12-screen.ppm",
            "milestone12-gles-screen.ppm",
            "milestone12-software-sdl-probe.ppm",
            "milestone12-gles-sdl-probe.ppm",
            "milestone12-software-fullscreen.ppm",
            "milestone12-gles-fullscreen.ppm",
            "milestone12-software-cursor.ppm",
            "milestone12-gles-cursor.ppm",
            "milestone12-software-testsprite.ppm",
            "milestone12-gles-testsprite.ppm",
        },
        "expected frame artifact set differs",
        errors,
    )
    require(
        set(document.get("environment", ())) == REQUIRED_ENVIRONMENT,
        "runtime environment differs from the frozen profile",
        errors,
    )
    require(
        document.get("official_program_compile_flags")
        == ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"],
        "official program compile flags differ",
        errors,
    )
    sources = document.get("source", [])
    require(len(sources) == 1, "exactly one SDL source is required", errors)
    if len(sources) == 1:
        source = sources[0]
        require(source.get("application") == "SDL", "source must be SDL", errors)
        require(source.get("version") == "2.32.10", "SDL version must be 2.32.10", errors)
        require(source.get("source_sha256") == SDL_ARCHIVE_SHA256, "archive hash differs", errors)
        require(source.get("source_size_bytes") == 7630262, "archive size differs", errors)
        require(source.get("source_signature_sha256") == SDL_SIGNATURE_SHA256, "signature hash differs", errors)
        require(source.get("signing_key_fingerprint") == SDL_SIGNING_FINGERPRINT, "signing fingerprint differs", errors)
        build_arguments = set(source.get("build_arguments", ()))
        require(REQUIRED_BUILD_ARGUMENTS <= build_arguments, "required SDL build arguments are missing", errors)
        require(len(build_arguments) == len(source.get("build_arguments", ())), "duplicate SDL build argument", errors)
        require(
            source.get("xfixes_runtime_evidence") == "raw-xcb-only",
            "XFIXES runtime evidence boundary differs",
            errors,
        )

    programs = {item.get("name"): item for item in document.get("program", [])}
    require(set(programs) == {"testdraw2", "testsprite2"}, "official program set differs", errors)
    for program in programs.values():
        path = program.get("source_path", "")
        require(program.get("source_sha256") == PROGRAM_HASHES.get(path), f"hash differs for {path}", errors)
        require(
            program.get("expected_extension_queries") == OFFICIAL_EXTENSION_QUERIES,
            f"extension query profile differs for {path}",
            errors,
        )
    sprite = programs.get("testsprite2", {})
    require(sprite.get("asset_sha256") == PROGRAM_HASHES["test/icon.bmp"], "icon.bmp hash differs", errors)
    require("--iterations" in sprite.get("argv", ()), "testsprite2 must use bounded iterations", errors)

    for candidate, expected, label in (
        (arguments.archive, SDL_ARCHIVE_SHA256, "archive"),
        (arguments.signature, SDL_SIGNATURE_SHA256, "signature"),
    ):
        if candidate is not None:
            require(candidate.is_file(), f"{label} is not a regular file", errors)
            if candidate.is_file():
                require(digest(candidate) == expected, f"{label} content hash differs", errors)

    if arguments.source_tree is not None:
        for relative, expected in PROGRAM_HASHES.items():
            candidate = arguments.source_tree / relative
            require(candidate.is_file(), f"source file is missing: {relative}", errors)
            if candidate.is_file():
                require(digest(candidate) == expected, f"source hash differs: {relative}", errors)

    if errors:
        for error in errors:
            print(f"m12 manifest: {error}", file=sys.stderr)
        return 1
    print("m12 SDL manifest: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
