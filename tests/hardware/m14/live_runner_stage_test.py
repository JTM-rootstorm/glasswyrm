#!/usr/bin/env python3
"""Prove the complete runner composes the frozen stage order."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile


MODULE_ROOT = Path(__file__).resolve().parents[3] / "tools" / "gw-hw.d"
sys.path.insert(0, str(MODULE_ROOT))
from live_runner import (  # noqa: E402
    DIAGNOSTIC_STACK_STAGES, FULL_ACCEPTANCE_STAGES, FixedLiveRunner,
)


CONFIG = {
    "tty": "/dev/tty2",
    "alternate_tty": "/dev/tty1",
}


def main() -> int:
    expected = (
        "start-stack", "stack-cadence", "policy-matrix", "vt-cycle",
        "restart-gwm", "restart-gwcomp", "pixel-parity",
        "shutdown-and-restore",
    )
    assert FULL_ACCEPTANCE_STAGES == expected
    with tempfile.TemporaryDirectory() as temporary:
        events: list[str] = []
        runner = FixedLiveRunner(
            CONFIG, Path(temporary), execute=lambda _argv, _output: 0,
            verify_paths=False, validate_runtime=False,
        )
        runner.preflight = lambda: events.append("preflight")
        for stage in FULL_ACCEPTANCE_STAGES:
            name = "stage_" + stage.replace("-", "_")

            def record(value: str = stage) -> None:
                events.append(value)
                if value == "shutdown-and-restore":
                    runner.cleanup_attempted = True

            setattr(runner, name, record)
        runner.run()
        assert events == ["preflight", *expected]

        for selected in DIAGNOSTIC_STACK_STAGES:
            events = []
            stage_runner = FixedLiveRunner(
                CONFIG, Path(temporary), execute=lambda _argv, _output: 0,
                verify_paths=False, validate_runtime=False,
            )
            stage_runner.preflight = lambda: events.append("preflight")
            stage_runner.stage_start_stack = lambda: events.append("start-stack")
            stage_runner.stage_stack_cadence = lambda: events.append(
                "stack-cadence")
            stage_runner.stage_policy_matrix = lambda: events.append(
                "policy-matrix")
            stage_runner.stage_vt_cycle = lambda: events.append("vt-cycle")
            stage_runner.stage_restart_gwm = lambda: events.append(
                "restart-gwm")
            stage_runner.stage_restart_gwcomp = lambda: events.append(
                "restart-gwcomp")
            stage_runner.stage_pixel_parity = lambda: events.append(
                "pixel-parity")
            stage_runner.prepare_always_eligible_stage = lambda: events.append(
                "prepare-always")

            def restored() -> None:
                events.append("cleanup")
                stage_runner.cleanup_attempted = True
                stage_runner.restoration_evidence = {"passed": True}

            stage_runner.cleanup = restored
            stage_runner.set_policy = lambda policy: events.append(
                f"policy-{policy}")
            stage_runner.run_stage(selected)
            assert events[:2] == ["preflight", "start-stack"]
            assert events[-1] == "cleanup"
            assert selected in events
            if selected == "stack-cadence":
                assert "restart-gwm" not in events
                assert "restart-gwcomp" not in events
    print("M14 live runner stage composition: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
