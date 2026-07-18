#!/usr/bin/env python3
"""Exercise accepted-evidence fixture promotion and failed-run rejection."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import tarfile
import tempfile


def load():
    path = pathlib.Path(__file__).with_name("promote_fixtures.py")
    spec = importlib.util.spec_from_file_location("promote_fixtures", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load fixture promoter")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_json(path: pathlib.Path, value: dict) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n")


def registry(order: str) -> dict:
    return {
        "schema": 1, "probe": "m12_raw_probe", "scenario": "registry",
        "byte_order": order, "checks": {"registry": True},
        "evidence": {"assignments": {"RANDR": {"present": True}},
                     "list_extensions": ["RANDR"]},
        "passed": True,
    }


def renderer(selection: dict, frame: dict) -> str:
    return json.dumps(selection, sort_keys=True) + "\n" + \
        json.dumps(frame, sort_keys=True) + "\n"


def make_evidence(promoter, root: pathlib.Path) -> pathlib.Path:
    evidence = root / "evidence"
    evidence.mkdir()
    write_json(evidence / "milestone12-raw-little-registry.json", registry("little"))
    write_json(evidence / "milestone12-raw-big-registry.json", registry("big"))
    write_json(evidence / "milestone12-extension-trace.json", {
        "schema": 1, "profiles": {"shm": {}, "no-shm": {}},
        "passed": True, "evidence_errors": [],
    })
    for workload in ("testdraw2", "testsprite2"):
        write_json(evidence / f"milestone12-{workload}-trace.json", {
            "schema": 1, "workload": workload, "recurring_image_classes": [
                {"transport": "MIT-SHM", "minor": 3, "request_length": 40}
            ], "unexpected_errors": [], "passed": True,
        })
    write_json(evidence / "milestone12-sdl-probe.json", {
        "schema": 1, "probe": "m12_sdl_probe", "video_driver": "x11",
        "display_count": 1, "checks": {"initialized": True}, "passed": True,
    })
    ppm = b"P6\n1 1\n255\n\x12\x34\x56"
    for name in ("milestone12-software-testsprite.ppm",
                 "milestone12-gles-testsprite.ppm",
                 "milestone12-software-fullscreen.ppm",
                 "milestone12-gles-fullscreen.ppm"):
        (evidence / name).write_bytes(ppm)
    software_selection = {
        "record": "selection", "requested": "software", "selected": "software",
        "egl_platform": None, "egl_vendor": None, "egl_version": None,
        "gles_version": None, "gl_vendor": None, "gl_renderer": None,
        "gl_version": None, "gbm_device": None, "render_node": None,
    }
    software_frame = {
        "record": "frame", "selected": "software", "disposition": "complete",
        "error": None, "texture_uploads": 0, "texture_upload_bytes": 0,
        "damage_rectangles": 1, "readback_bytes": 0, "texture_cache_bytes": 0,
    }
    (evidence / "milestone12-renderer-software.jsonl").write_text(
        renderer(software_selection, software_frame)
    )
    gles_selection = {
        "record": "selection", "requested": "gles", "selected": "gles",
        "egl_platform": "surfaceless", "egl_vendor": "Mesa",
        "egl_version": "1.5", "gles_version": "OpenGL ES 2.0",
        "gl_vendor": "Mesa", "gl_renderer": "llvmpipe",
        "gl_version": "OpenGL ES 3.2", "gbm_device": None,
        "render_node": None, "software_renderer": True,
    }
    gles_frame = {
        "record": "frame", "selected": "gles", "disposition": "complete",
        "error": None, "texture_uploads": 1, "texture_upload_bytes": 4,
        "damage_rectangles": 1, "readback_bytes": 4, "texture_cache_bytes": 4,
    }
    (evidence / "milestone12-renderer-gles.jsonl").write_text(
        renderer(gles_selection, gles_frame)
    )
    common_copy = {
        "record": "damage-copy", "generation": 1, "buffer": 0,
        "framebuffer_id": 1, "full_frame_bytes": 100, "copied_bytes": 100,
        "history_span": 1, "cumulative_full_frame_bytes": 100,
        "cumulative_copied_bytes": 100, "cumulative_copy_ratio_ppm": 1000000,
        "copy_rectangles": [],
    }
    drm = [
        {"record": "modeset", "canonical_hash": "same", "scanout_hash": "same"},
        {**common_copy, "full_copy_reason": "first-use"},
        {**common_copy, "generation": 2, "full_copy_reason": "vt-resume"},
        {**common_copy, "generation": 3, "full_copy_reason": "none",
         "copied_bytes": 4, "copy_rectangles": [{"x": 0, "y": 0,
                                                   "width": 1, "height": 1}]},
        {"record": "vt", "transition": "release"},
        {"record": "vt", "transition": "acquire", "master_owned": True,
         "full_modeset": True},
        {"record": "restore", "kms": True, "vt": True,
         "master_drop": True, "framebuffer_cleanup": True},
    ]
    (evidence / "milestone12-drm-damage-report.jsonl").write_text(
        "".join(json.dumps(record, sort_keys=True) + "\n" for record in drm)
    )
    names = sorted(path.name for path in evidence.iterdir())
    (evidence / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256((evidence / name).read_bytes()).hexdigest()}  {name}\n"
        for name in names
    ))
    archive = root / "milestone12-efficient-sdl-evidence.tar"
    with tarfile.open(archive, "w") as output:
        for path in sorted(evidence.iterdir()):
            output.add(path, arcname=path.name)
    return archive


def main() -> int:
    promoter = load()
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-promote-") as temporary:
        root = pathlib.Path(temporary)
        make_evidence(promoter, root)
        contract = root / "result-contract.json"
        write_json(contract, {"schema": 1})
        summary = root / "milestone12-summary.json"
        accepted = {
            "required_base_commit": promoter.REQUIRED_BASE,
            "tested_commit": "1" * 40,
            "facts": {"scenario_exit": "0", "sdl_source_sha256": "2" * 64},
            "evidence_errors": [], "passed": True,
        }
        write_json(summary, accepted)
        output = root / "output"
        promoter.promote(root, output, contract)
        expected = set(promoter.SOURCE_MAP) | {
            "README.md", "result-contract.json", "SHA256SUMS",
        }
        if {path.name for path in output.iterdir()} != expected:
            return 1
        if "promote_fixtures.py \\\n  --artifact-dir" not in (
            output / "README.md"
        ).read_text():
            return 1
        accepted["passed"] = False
        write_json(summary, accepted)
        try:
            promoter.promote(root, output, contract)
        except ValueError:
            pass
        else:
            return 1
    print("M12 fixture promotion tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
