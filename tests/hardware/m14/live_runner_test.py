#!/usr/bin/env python3

"""Focused regression tests for the M14 physical live-runner lifecycle."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
from typing import Callable


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "gw-hw.d"))

from common import HarnessError  # noqa: E402
from live_runner import (  # noqa: E402
    FIXED_BINARIES, LIVE_MANAGED_UNITS, LIVE_UNITS, FixedLiveRunner,
)


CONFIG: dict[str, object] = {
    "drm_device": "/dev/dri/card0",
    "connector": "DP-1",
    "mode": "2560x1440@144000",
    "tty": "/dev/tty2",
    "alternate_tty": "/dev/tty1",
    "keyboard_device": "/dev/input/event0",
    "pointer_device": "/dev/input/event1",
    "target_refresh_hz": 70,
}
CONSOLE_STATE = {"active_vt": 2, "kd_mode": 0, "getty_active": True}
Execute = Callable[[list[str], Path | None], int]


def make_runner(artifacts: Path, execute: Execute,
                verify_paths: bool = False) -> FixedLiveRunner:
    return FixedLiveRunner(
        dict(CONFIG), artifacts, execute, "/dev/tty2", verify_paths,
        lambda path, kind: True, lambda: dict(CONSOLE_STATE), False,
    )


def is_systemctl(argv: list[str], verb: str) -> bool:
    return (len(argv) >= 2 and argv[0] == str(FIXED_BINARIES["systemctl"])
            and argv[1] == verb)


def launched_unit(argv: list[str]) -> str | None:
    if not argv or argv[0] != str(FIXED_BINARIES["systemd-run"]):
        return None
    return next((argument.removeprefix("--unit=") + ".service"
                 for argument in argv if argument.startswith("--unit=")), None)


def write_unit_state(output: Path | None, load: str, active: str) -> None:
    assert output is not None
    output.write_text(
        f"LoadState={load}\nActiveState={active}\n", encoding="ascii",
    )


def expect_harness_error(action: Callable[[], None], message: str) -> None:
    try:
        action()
    except HarnessError as error:
        assert str(error) == message
    else:
        raise AssertionError("expected the fixed live runner to fail closed")


def test_start_unit_contract(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        return 0

    runner = make_runner(root, execute)
    runner.start_unit(
        LIVE_UNITS["gwm"], "gwm", ["--ipc-socket", "/run/test.sock"],
        ["KillMode=mixed"],
    )

    assert len(calls) == 1
    argv, output = calls[0]
    log = root / "gwm-m14-hardware.log"
    assert argv[0] == str(FIXED_BINARIES["systemd-run"])
    assert "--unit=gwm-m14-hardware" in argv
    assert "--property=Type=exec" in argv
    assert "--property=Type=simple" not in argv
    assert "--no-block" not in argv
    assert "--collect" in argv
    assert "--property=KillMode=mixed" in argv
    assert f"--property=StandardOutput=append:{log}" in argv
    assert f"--property=StandardError=append:{log}" in argv
    assert output == log
    separator = argv.index("--")
    assert argv[separator + 1:] == [
        str(FIXED_BINARIES["gwm"]), "--ipc-socket", "/run/test.sock",
    ]


def test_compositor_restart_recreates_transient_unit(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        return 0

    runner = make_runner(root, execute)
    runner.start_stack_after_gwm()
    runner.command([
        str(FIXED_BINARIES["systemctl"]), "stop", LIVE_UNITS["gwcomp"],
    ])
    runner.start_stack_after_gwm()

    launches = [argv for argv, _ in calls
                if launched_unit(argv) == LIVE_UNITS["gwcomp"]]
    assert len(launches) == 2
    assert all("--collect" in argv for argv in launches)
    assert [str(FIXED_BINARIES["systemctl"]), "start",
            LIVE_UNITS["gwcomp"]] not in [argv for argv, _ in calls]


def test_prepare_unit_names_reclaims_one_failed_unit(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []
    stale_queries = 0

    def execute(argv: list[str], output: Path | None) -> int:
        nonlocal stale_queries
        calls.append((argv, output))
        if is_systemctl(argv, "show"):
            name = argv[-1]
            if name == LIVE_UNITS["gwm"] and stale_queries == 0:
                stale_queries += 1
                write_unit_state(output, "loaded", "failed")
            else:
                write_unit_state(output, "not-found", "inactive")
        return 0

    runner = make_runner(root, execute)
    runner.prepare_unit_names()

    reset_calls = [argv for argv, _ in calls
                   if is_systemctl(argv, "reset-failed")]
    assert reset_calls == [[
        str(FIXED_BINARIES["systemctl"]), "reset-failed", LIVE_UNITS["gwm"],
    ]]
    inspected = [argv[-1] for argv, _ in calls if is_systemctl(argv, "show")]
    assert inspected.count(LIVE_UNITS["gwm"]) == 2
    assert len(inspected) == len(LIVE_MANAGED_UNITS) + 1


def test_prepare_unit_names_rejects_active_collision(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        if is_systemctl(argv, "show"):
            if argv[-1] == LIVE_UNITS["gwcomp"]:
                write_unit_state(output, "loaded", "active")
            else:
                write_unit_state(output, "not-found", "inactive")
        return 0

    runner = make_runner(root, execute)
    expect_harness_error(
        runner.prepare_unit_names,
        "fixed transient unit name is already in use: "
        f"{LIVE_UNITS['gwcomp']} (loaded/active)",
    )
    assert not any(is_systemctl(argv, "reset-failed") for argv, _ in calls)


def test_stop_unit_verifies_failed_stop_state(root: Path) -> None:
    accepted_states = (
        ("not-found", "inactive", False),
        ("loaded", "inactive", True),
        ("loaded", "failed", True),
    )
    for index, (load, active, expect_reset) in enumerate(accepted_states):
        case_root = root / f"accepted-{index}"
        case_root.mkdir()
        calls: list[list[str]] = []
        state_queries = 0

        def accepted(argv: list[str], output: Path | None,
                     load: str = load, active: str = active) -> int:
            nonlocal state_queries
            calls.append(argv)
            if is_systemctl(argv, "stop"):
                return 1
            if is_systemctl(argv, "show"):
                state_queries += 1
                if state_queries == 1:
                    write_unit_state(output, load, active)
                else:
                    write_unit_state(output, "not-found", "inactive")
            return 0

        runner = make_runner(case_root, accepted, True)
        runner.managed_units = [LIVE_UNITS["gwm"]]
        runner.stop_unit(LIVE_UNITS["gwm"])
        assert runner.managed_units == []
        reset_calls = [argv for argv in calls
                       if is_systemctl(argv, "reset-failed")]
        expected_reset = [[
            str(FIXED_BINARIES["systemctl"]), "reset-failed",
            LIVE_UNITS["gwm"],
        ]] if expect_reset else []
        assert reset_calls == expected_reset

    for label, load, active in (
            ("active", "loaded", "active"),
            ("not-found-active", "not-found", "active")):
        case_root = root / label
        case_root.mkdir()

        def still_running(argv: list[str], output: Path | None,
                          load: str = load, active: str = active) -> int:
            if is_systemctl(argv, "stop"):
                return 1
            if is_systemctl(argv, "show"):
                write_unit_state(output, load, active)
            return 0

        runner = make_runner(case_root, still_running, True)
        runner.managed_units = [LIVE_UNITS["gwm"]]
        expect_harness_error(
            lambda: runner.stop_unit(LIVE_UNITS["gwm"]),
            (f"fixed transient unit did not unload after stop: "
             f"{LIVE_UNITS['gwm']} (stop=1, {load}/{active})"),
        )
        assert runner.managed_units == [LIVE_UNITS["gwm"]]

    malformed_root = root / "malformed"
    malformed_root.mkdir()

    def malformed(argv: list[str], output: Path | None) -> int:
        if is_systemctl(argv, "stop"):
            return 1
        if is_systemctl(argv, "show"):
            assert output is not None
            output.write_text("LoadState=loaded\n", encoding="ascii")
        return 0

    malformed_runner = make_runner(malformed_root, malformed, True)
    malformed_runner.managed_units = [LIVE_UNITS["gwm"]]
    expect_harness_error(
        lambda: malformed_runner.stop_unit(LIVE_UNITS["gwm"]),
        f"fixed transient unit omitted exact state: {LIVE_UNITS['gwm']}",
    )
    assert malformed_runner.managed_units == [LIVE_UNITS["gwm"]]


def test_first_launch_failure_cleanup(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        if is_systemctl(argv, "is-active"):
            return 1
        if launched_unit(argv) == LIVE_UNITS["gwm"]:
            assert output == root / "gwm-m14-hardware.log"
            output.write_text("fixture launch rejection\n", encoding="utf-8")
            return 1
        return 0

    runner = make_runner(root, execute)
    expect_harness_error(
        runner.run,
        "fixed command failed: systemd-run",
    )

    assert runner.cleanup_attempted
    stopped = [argv[2] for argv, _ in calls
               if is_systemctl(argv, "stop") and len(argv) >= 3]
    assert LIVE_UNITS["gwm"] not in stopped
    assert LIVE_UNITS["gwcomp"] not in stopped
    assert LIVE_UNITS["server"] not in stopped
    assert [str(FIXED_BINARIES["chvt"]), "2"] in [argv for argv, _ in calls]
    assert [str(FIXED_BINARIES["systemctl"]), "start",
            "getty@tty2.service"] in [argv for argv, _ in calls]
    assert runner.cleanup_errors == []
    assert runner.restoration_evidence is not None
    assert runner.restoration_evidence["passed"] is True
    assert (root / "gwm-m14-hardware.log").read_text(
        encoding="utf-8") == "fixture launch rejection\n"


def test_second_launch_failure_cleanup(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        if is_systemctl(argv, "is-active"):
            return 1
        if launched_unit(argv) == LIVE_UNITS["gwcomp"]:
            return 1
        return 0

    runner = make_runner(root, execute)
    expect_harness_error(
        runner.run,
        "fixed command failed: systemd-run",
    )

    assert runner.cleanup_attempted
    stopped = [argv[2] for argv, _ in calls
               if is_systemctl(argv, "stop") and len(argv) >= 3]
    assert stopped.count(LIVE_UNITS["gwm"]) == 1
    assert LIVE_UNITS["gwcomp"] not in stopped
    assert LIVE_UNITS["server"] not in stopped
    assert runner.cleanup_errors == []
    assert runner.restoration_evidence is not None
    assert runner.restoration_evidence["passed"] is True


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        contract = root / "contract"
        contract.mkdir()
        test_start_unit_contract(contract)

        restart = root / "restart"
        restart.mkdir()
        test_compositor_restart_recreates_transient_unit(restart)

        stale = root / "stale"
        stale.mkdir()
        test_prepare_unit_names_reclaims_one_failed_unit(stale)

        collision = root / "collision"
        collision.mkdir()
        test_prepare_unit_names_rejects_active_collision(collision)

        stop_state = root / "stop-state"
        stop_state.mkdir()
        test_stop_unit_verifies_failed_stop_state(stop_state)

        first_failure = root / "first-failure"
        first_failure.mkdir()
        test_first_launch_failure_cleanup(first_failure)

        second_failure = root / "second-failure"
        second_failure.mkdir()
        test_second_launch_failure_cleanup(second_failure)
    print("m14 live-runner lifecycle: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
