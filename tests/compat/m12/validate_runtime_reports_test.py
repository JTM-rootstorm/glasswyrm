#!/usr/bin/env python3
"""Focused success/failure coverage for the M12 runtime report validator."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def write_jsonl(path: pathlib.Path, values: list[dict[str, object]]) -> None:
    path.write_text("".join(json.dumps(value) + "\n" for value in values))


def run(validator: pathlib.Path, root: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(validator), "--software-renderer", str(root / "software.jsonl"),
         "--gles-renderer", str(root / "gles.jsonl"), "--drm-report", str(root / "drm.jsonl"),
         "--sync-observation", str(root / "sync.json"), "--output-dir", str(root / "out")],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )


def main() -> int:
    validator = pathlib.Path(__file__).with_name("validate_runtime_reports.py")
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-reports-") as temporary:
        root = pathlib.Path(temporary)
        write_jsonl(root / "software.jsonl", [
            {"record": "selection", "requested": "software", "selected": "software",
             "egl_platform": None, "egl_vendor": None, "egl_version": None,
             "gles_version": None, "gl_vendor": None, "gl_renderer": None,
             "gl_version": None, "gbm_device": None, "render_node": None,
             "software_renderer": True, "fallback_reasons": []},
            {"record": "frame", "selected": "software", "commit_id": 1, "generation": 1,
             "ordinal": 1, "disposition": "complete", "texture_uploads": 0,
             "texture_upload_bytes": 0, "damage_rectangles": 1, "readback_bytes": 0,
             "texture_cache_bytes": 0, "fallback_reason": None, "error": None},
        ])
        write_jsonl(root / "gles.jsonl", [
            {"record": "selection", "requested": "gles", "selected": "gles",
             "egl_platform": "surfaceless", "egl_vendor": "Mesa", "egl_version": "1.5",
             "gles_version": "OpenGL ES 3.2", "gl_vendor": "Mesa",
             "gl_renderer": "llvmpipe", "gl_version": "OpenGL ES 3.2 Mesa",
             "gbm_device": None, "render_node": None, "software_renderer": True,
             "fallback_reasons": []},
            {"record": "frame", "selected": "gles", "commit_id": 1, "generation": 1,
             "ordinal": 1, "disposition": "complete", "texture_uploads": 1,
             "texture_upload_bytes": 64, "damage_rectangles": 1, "readback_bytes": 64,
             "texture_cache_bytes": 64, "fallback_reason": None, "error": None},
        ])
        frames = [
            {"record": "modeset", "canonical_hash": "aa", "scanout_hash": "aa"},
            {"record": "vt", "transition": "release", "master_owned": False, "full_modeset": False},
            {"record": "vt", "transition": "acquire", "master_owned": True, "full_modeset": True},
            {"record": "restore", "kms": True, "vt": True, "master_drop": True,
             "framebuffer_cleanup": True},
        ]
        for generation, reason, copied in ((1, "first-use", 100), (2, "none", 20), (3, "vt-resume", 100)):
            frames.append({"record": "damage-copy", "generation": generation, "buffer": 0,
                           "framebuffer_id": 2, "full_frame_bytes": 100,
                           "copied_bytes": copied, "copy_rectangles": [{"x": 0, "y": 0,
                           "width": 5, "height": 1}], "history_span": 1,
                           "full_copy_reason": reason, "cumulative_full_frame_bytes": 100 * generation,
                           "cumulative_copied_bytes": copied, "cumulative_copy_ratio_ppm": copied * 10000})
        write_jsonl(root / "drm.jsonl", frames)
        (root / "sync.json").write_text(json.dumps({
            "schema": 1,
            "counts": {"eventfd_before": 0, "eventfd_live": 3, "eventfd_after": 0,
                       "producer_eventfd_live": 1, "consumer_eventfd_live": 1,
                       "shm_before": 1, "shm_live": 2, "shm_after": 1,
                       "live_processes_after": 0},
            "checks": {"eventfd_capability": True, "missing_token_wait": True,
                       "read_after_token": True, "runtime_sockets_removed": True,
                       "input_devices_removed": True, "texture_cache_released": True,
                       "device_fds_released": True},
        }))
        good = run(validator, root)
        if good.returncode != 0 or "passed" not in good.stdout:
            print(good.stdout, end="")
            return 1
        bad = json.loads((root / "sync.json").read_text())
        bad["counts"]["eventfd_after"] = 1
        (root / "sync.json").write_text(json.dumps(bad))
        failed = run(validator, root)
        if failed.returncode == 0 or "eventfd count did not return" not in failed.stdout:
            print(failed.stdout, end="")
            return 1
    print("M12 runtime report validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
