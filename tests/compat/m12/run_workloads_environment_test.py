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
    validator_path = pathlib.Path(__file__).with_name("validate_result.py")
    validator_spec = importlib.util.spec_from_file_location(
        "m12_validate_result", validator_path
    )
    if validator_spec is None or validator_spec.loader is None:
        return 1
    validator = importlib.util.module_from_spec(validator_spec)
    validator_spec.loader.exec_module(validator)
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-environment-") as temporary:
        shim = pathlib.Path(temporary) / "tests" / "libgw_m9_fixed_time.so"
        shim.parent.mkdir()
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
        evidence_environment = module.frozen_environment({}, 99, shim)
        errors: list[str] = []
        validator.validate_workload_environment(
            {"environment": evidence_environment}, errors
        )
        if errors:
            return 1
        evidence_environment["LD_PRELOAD"] = "/untrusted.so"
        validator.validate_workload_environment(
            {"environment": evidence_environment}, errors
        )
        if not errors:
            return 1
    print("M12 workload environment tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
