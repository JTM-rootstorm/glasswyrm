#!/usr/bin/env python3
"""Focused coverage for the frozen M12 child-process environment."""

from __future__ import annotations

import importlib.util
import pathlib
import tempfile


def main() -> int:
    module_path = pathlib.Path(__file__).with_name("run_workloads.py")
    spec = importlib.util.spec_from_file_location("m12_run_workloads", module_path)
    if spec is None or spec.loader is None:
        return 1
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-environment-") as temporary:
        shim = pathlib.Path(temporary) / "libfixed-time.so"
        shim.write_bytes(b"test shim identity")
        environment = module.frozen_environment(
            {"PATH": "/test/bin", "LD_PRELOAD": "/untrusted.so"}, 77, shim
        )
        expected = {
            **module.FROZEN_ENVIRONMENT,
            "DISPLAY": ":77",
            "LD_PRELOAD": str(shim.resolve()),
        }
        if any(environment.get(key) != value for key, value in expected.items()):
            return 1
        if environment.get("PATH") != "/test/bin":
            return 1
    print("M12 workload environment tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
