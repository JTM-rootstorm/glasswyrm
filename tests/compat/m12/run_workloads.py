#!/usr/bin/env python3
"""Run the fixed M12 raw, XCB, SDL, and official SDL workloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import signal
import subprocess
import time
from typing import Any


FROZEN_ENVIRONMENT = {
    "LC_ALL": "C",
    "LANG": "C",
    "XMODIFIERS": "@im=none",
    "SESSION_MANAGER": "",
    "XAUTHORITY": "/dev/null",
    "DISPLAY": ":99",
    "SDL_VIDEODRIVER": "x11",
    "SDL_RENDER_DRIVER": "software",
    "SDL_AUDIODRIVER": "dummy",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(
    name: str,
    argv: list[str],
    log: pathlib.Path,
    environment: dict[str, str],
    cwd: pathlib.Path,
    timeout: float,
    continuous: bool = False,
) -> dict[str, Any]:
    timed_out = False
    exit_code: int | None = None
    error = ""
    try:
        with log.open("wb") as output:
            process = subprocess.Popen(
                argv,
                cwd=cwd,
                env=environment,
                stdout=output,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            try:
                exit_code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    exit_code = process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    exit_code = process.wait(timeout=3)
    except (OSError, subprocess.SubprocessError) as failure:
        error = str(failure)
        log.write_text(error + "\n")
    text = log.read_text(errors="replace") if log.is_file() else ""
    protocol_error = any(
        marker in text
        for marker in ("X Error", "XIO:", "BadRequest", "BadLength", "segmentation fault")
    )
    passed = not error and not protocol_error and (
        (continuous and (timed_out or exit_code == 0))
        or (not continuous and exit_code == 0)
    )
    return {
        "name": name,
        "argv": argv,
        "exit_code": exit_code,
        "bounded_timeout": timed_out,
        "continuous_workload": continuous,
        "protocol_error": protocol_error,
        "log": log.name,
        "log_sha256": sha256(log),
        "passed": passed,
        "error": error,
    }


def stop(process: subprocess.Popen[bytes]) -> int:
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            return process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
    return process.wait(timeout=3)


def run_resident_workloads(
    programs: pathlib.Path,
    artifacts: pathlib.Path,
    control: pathlib.Path,
    environment: dict[str, str],
) -> list[dict[str, Any]]:
    control.mkdir(parents=True, exist_ok=True)
    for name in ("fullscreen-ready", "exit-fullscreen", "borderless-ready",
                 "resident-ready.json", "resident-release"):
        (control / name).unlink(missing_ok=True)
    sdl_log = artifacts / "resident-sdl.log"
    sprite_log = artifacts / "resident-testsprite2.log"
    sdl_result = artifacts / "resident-sdl.json"
    sdl_argv = [str(programs / "m12_sdl_probe"), "--output", str(sdl_result),
                "--control-dir", str(control)]
    sprite_argv = [str(programs / "testsprite2"), "--video", "x11", "--renderer",
                   "software", "--windows", "1", "--geometry", "640x480",
                   "--position", "64,64", "--blend", "none", "--iterations",
                   "120", "100", "icon.bmp"]
    with sdl_log.open("wb") as sdl_output, sprite_log.open("wb") as sprite_output:
        sprite = subprocess.Popen(sprite_argv, cwd=programs, env=environment,
                                  stdout=sprite_output, stderr=subprocess.STDOUT,
                                  start_new_session=True)
        sdl = subprocess.Popen(sdl_argv, cwd=programs, env=environment,
                               stdout=sdl_output, stderr=subprocess.STDOUT,
                               start_new_session=True)
        ready = False
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if sdl.poll() is not None or sprite.poll() is not None:
                break
            if (control / "fullscreen-ready").is_file():
                ready = True
                break
            time.sleep(0.02)
        if ready:
            (control / "resident-ready.json").write_text(json.dumps({
                "schema": 1, "sdl_pid": sdl.pid, "testsprite2_pid": sprite.pid,
                "fullscreen": True,
            }, sort_keys=True) + "\n")
        deadline = time.monotonic() + 240
        while ready and time.monotonic() < deadline:
            if (control / "resident-release").is_file() and sdl.poll() is not None:
                break
            if sprite.poll() is not None:
                ready = False
                break
            time.sleep(0.02)
        released = (control / "resident-release").is_file()
        sdl_code = stop(sdl)
        sprite_was_alive = sprite.poll() is None
        sprite_code = stop(sprite)
    sdl_text = sdl_log.read_text(errors="replace")
    sprite_text = sprite_log.read_text(errors="replace")
    try:
        sdl_payload = json.loads(sdl_result.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError):
        sdl_payload = {}
    sdl_passed = (ready and released and sdl_code == 0 and
                  sdl_payload.get("passed") is True and
                  "X Error" not in sdl_text)
    sprite_passed = (ready and released and sprite_was_alive and
                     sprite_code in (0, -signal.SIGTERM) and
                     "X Error" not in sprite_text and "XIO:" not in sprite_text)
    return [
        {"name": "resident-sdl-public-api", "argv": sdl_argv,
         "exit_code": sdl_code, "bounded_timeout": False,
         "continuous_workload": True, "protocol_error": "X Error" in sdl_text,
         "log": sdl_log.name, "log_sha256": sha256(sdl_log),
         "passed": sdl_passed, "error": "" if sdl_passed else "resident SDL handshake failed"},
        {"name": "resident-testsprite2", "argv": sprite_argv,
         "exit_code": sprite_code, "bounded_timeout": False,
         "continuous_workload": True,
         "protocol_error": "X Error" in sprite_text or "XIO:" in sprite_text,
         "log": sprite_log.name, "log_sha256": sha256(sprite_log),
         "passed": sprite_passed,
         "error": "" if sprite_passed else "resident testsprite2 did not survive the live cycles"},
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("shm", "no-shm"), required=True)
    parser.add_argument("--program-dir", type=pathlib.Path, required=True)
    parser.add_argument("--artifact-dir", type=pathlib.Path, required=True)
    parser.add_argument("--display", type=int, default=99)
    parser.add_argument("--raw-probe", type=pathlib.Path)
    parser.add_argument("--official-timeout", type=float, default=5.0)
    parser.add_argument("--resident-control-dir", type=pathlib.Path)
    arguments = parser.parse_args()

    here = pathlib.Path(__file__).resolve().parent
    raw_probe = arguments.raw_probe or here / "m12_raw_probe.py"
    artifacts = arguments.artifact_dir.resolve()
    programs = arguments.program_dir.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(FROZEN_ENVIRONMENT)
    environment["DISPLAY"] = f":{arguments.display}"

    workloads: list[dict[str, Any]] = []
    raw_results: list[dict[str, Any]] = []
    raw_scenarios = [
        "registry", "big-requests", "xfixes-damage", "randr", "errors"
    ]
    if arguments.profile == "shm":
        raw_scenarios[2:2] = ["shm", "render-composite"]
    for order in ("little", "big"):
        for scenario in raw_scenarios:
            name = f"raw-{order}-{scenario}"
            result_path = artifacts / f"{name}.json"
            argv = [
                "python3",
                str(raw_probe),
                "--byte-order",
                order,
                "--scenario",
                scenario,
                "--display",
                str(arguments.display),
                "--output",
                str(result_path),
            ]
            if arguments.profile == "no-shm" and scenario == "registry":
                argv.append("--expect-mit-shm-disabled")
            workload = run(
                name, argv, artifacts / f"{name}.log", environment, programs, 15
            )
            workloads.append(workload)
            if result_path.is_file():
                raw_results.append(json.loads(result_path.read_text()))

    if arguments.profile == "shm":
        workloads.append(
            run(
                "xcb-extensions",
                [str(programs / "m12_xcb_probe"), "--output", str(artifacts / "xcb.json")],
                artifacts / "xcb.log",
                environment,
                programs,
                15,
            )
        )

    workloads.append(
        run(
            "sdl-public-api",
            [str(programs / "m12_sdl_probe"), "--output", str(artifacts / "sdl.json")],
            artifacts / "sdl.log",
            environment,
            programs,
            20,
        )
    )

    if arguments.resident_control_dir:
        workloads.extend(run_resident_workloads(
            programs, artifacts, arguments.resident_control_dir.resolve(), environment
        ))
    workloads.append(
        run(
            "testdraw2",
            [
                str(programs / "testdraw2"),
                "--video", "x11", "--renderer", "software", "--windows", "1",
                "--geometry", "640x480", "--position", "64,64", "--blend", "none", "100",
            ],
            artifacts / "testdraw2.log",
            environment,
            programs,
            arguments.official_timeout,
            continuous=True,
        )
    )
    workloads.append(
        run(
            "testsprite2",
            [
                str(programs / "testsprite2"),
                "--video", "x11", "--renderer", "software", "--windows", "1",
                "--geometry", "640x480", "--position", "64,64", "--blend", "none",
                "--iterations", "120", "100", "icon.bmp",
            ],
            artifacts / "testsprite2.log",
            environment,
            programs,
            arguments.official_timeout,
            continuous=True,
        )
    )

    fallback = arguments.profile == "no-shm"
    fallback_registry = any(
        item.get("scenario") == "registry"
        and item.get("passed")
        and item.get("checks", {}).get("MIT-SHM")
        for item in raw_results
    )
    fallback_big_request = any(
        item.get("scenario") == "big-requests"
        and item.get("passed")
        and item.get("checks", {}).get("extended_put_image")
        for item in raw_results
    )
    checks = {
        "all_workloads": all(item["passed"] for item in workloads),
        "both_byte_orders": {
            item.get("byte_order") for item in raw_results if item.get("passed")
        } == {"little", "big"},
        "mit_shm_profile": arguments.profile == "shm" or fallback_registry,
        "fallback_big_requests": not fallback or fallback_big_request,
        "official_testdraw2": next(
            item["passed"] for item in workloads if item["name"] == "testdraw2"
        ),
        "official_testsprite2": next(
            item["passed"] for item in workloads if item["name"] == "testsprite2"
        ),
    }
    payload = {
        "schema": 1,
        "profile": arguments.profile,
        "environment": {key: environment[key] for key in FROZEN_ENVIRONMENT},
        "workloads": workloads,
        "checks": checks,
        "passed": all(checks.values()),
        "evidence_errors": [key for key, value in checks.items() if not value],
    }
    (artifacts / "m12-workloads.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )
    return 0 if payload["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
