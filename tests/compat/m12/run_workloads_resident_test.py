#!/usr/bin/env python3
"""Exercise the resident SDL/testsprite control handshake without X11."""

from __future__ import annotations

import importlib.util
import os
import pathlib
import tempfile
import threading
import time


def executable(path: pathlib.Path, body: str) -> None:
    path.write_text("#!/usr/bin/env python3\n" + body)
    path.chmod(0o755)


def wait(path: pathlib.Path) -> None:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.01)
    raise RuntimeError(f"timed out waiting for {path}")


def main() -> int:
    module_path = pathlib.Path(__file__).with_name("run_workloads.py")
    spec = importlib.util.spec_from_file_location("m12_run_workloads", module_path)
    if spec is None or spec.loader is None:
        return 1
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-resident-") as temporary:
        root = pathlib.Path(temporary)
        programs = root / "programs"
        artifacts = root / "artifacts"
        control = root / "control"
        programs.mkdir()
        artifacts.mkdir()
        (programs / "icon.bmp").write_bytes(b"fixture")
        executable(programs / "m12_sdl_probe", """
import json,pathlib,sys,time
args=dict(zip(sys.argv[1::2],sys.argv[2::2])); out=pathlib.Path(args['--output'])
control=pathlib.Path(args['--control-dir']); control.mkdir(exist_ok=True)
(control/'windowed-ready').write_text('ready\\n')
while not (control/'enter-fullscreen').exists(): time.sleep(.01)
(control/'fullscreen-ready').write_text('ready\\n')
while not (control/'exit-fullscreen').exists(): time.sleep(.01)
(control/'borderless-ready').write_text('ready\\n')
while not (control/'close').exists(): time.sleep(.01)
out.write_text(json.dumps({'passed':True})+'\\n')
""")
        executable(programs / "testsprite2", """
import time
while True: time.sleep(.05)
""")

        errors: list[BaseException] = []
        def control_handshake() -> None:
            try:
                wait(control / "resident-ready.json")
                (control / "enter-fullscreen").write_text("continue\n")
                wait(control / "fullscreen-ready")
                (control / "exit-fullscreen").write_text("continue\n")
                wait(control / "borderless-ready")
                (control / "close").write_text("close\n")
                wait(artifacts / "resident-sdl.json")
                (control / "resident-release").write_text("release\n")
            except BaseException as error:  # pragma: no cover - surfaced below
                errors.append(error)

        controller = threading.Thread(target=control_handshake)
        controller.start()
        results = module.run_resident_workloads(
            programs, artifacts, control, os.environ.copy()
        )
        controller.join(timeout=10)
        if controller.is_alive() or errors or len(results) != 2 or not all(
            item["passed"] for item in results
        ):
            print(errors, results)
            return 1
    print("M12 resident workload tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
