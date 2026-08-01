#!/usr/bin/env python3
"""Prove the complete runner composes the frozen stage order."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile


MODULE_ROOT = Path(__file__).resolve().parents[3] / "tools" / "gw-hw.d"
sys.path.insert(0, str(MODULE_ROOT))
from live_runner import FULL_ACCEPTANCE_STAGES, FixedLiveRunner  # noqa: E402


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
    print("M14 live runner stage composition: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
