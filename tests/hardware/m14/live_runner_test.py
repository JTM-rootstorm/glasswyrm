#!/usr/bin/env python3

"""Focused regression tests for the M14 physical live-runner lifecycle."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
from typing import Callable


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "gw-hw.d"))

from common import HarnessError  # noqa: E402
from live_runner import (  # noqa: E402
    CLEANUP_QUERY_ATTEMPTS, CLIENT_RESULT_WAIT_ATTEMPTS, COMMAND_TAIL_BYTES,
    FIXED_BINARIES, LIVE_MANAGED_UNITS, LIVE_UNITS, QUERY_ATTEMPTS,
    QUERY_DIAGNOSTIC_SCHEMA, CommandResult, FixedLiveRunner,
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
Execute = Callable[[list[str], Path | None], int | CommandResult]


def make_runner(artifacts: Path, execute: Execute,
                verify_paths: bool = False,
                validate_runtime: bool = False) -> FixedLiveRunner:
    return FixedLiveRunner(
        dict(CONFIG), artifacts, execute, "/dev/tty2", verify_paths,
        lambda path, kind: True, lambda: dict(CONSOLE_STATE), validate_runtime,
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


def expect_harness_error_contains(action: Callable[[], None], message: str) -> None:
    try:
        action()
    except HarnessError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError("expected the fixed live runner to fail closed")


def query_result(output: Path | None, *, status: int | None = 0,
                 terminating_signal: int | None = None,
                 timed_out: bool = False, stdout_bytes: int | None = None,
                 stderr_bytes: int = 0, elapsed_ns: int = 10,
                 tail: str = "") -> CommandResult:
    if stdout_bytes is None:
        stdout_bytes = output.stat().st_size if output and output.exists() else 0
    return CommandResult(
        exit_status=status,
        signal=terminating_signal,
        timed_out=timed_out,
        stdout_bytes=stdout_bytes,
        stderr_bytes=stderr_bytes,
        elapsed_ns=elapsed_ns,
        output_path=output,
        bounded_tail=tail,
    )


def write_snapshot(output: Path | None, policy: str = "always-eligible",
                   effective: bool = True) -> None:
    assert output is not None
    output.write_text(
        '{"vrr":[{"name":"DP-1","policy":"' + policy + '",'
        '"effective_enabled":' + str(effective).lower() + ','
        '"hardware_capable":true,"kms_controllable":true,'
        '"simulated":false}],"windows":[]}\n',
        encoding="utf-8",
    )


def read_query_diagnostic(root: Path, name: str) -> dict[str, object]:
    path = root / ".query-diagnostics" / f"{name}.diagnostic.json"
    return json.loads(path.read_text(encoding="utf-8"))


def test_command_result_contract(root: Path) -> None:
    output = root / "query.json"
    output.write_text("{}\n", encoding="utf-8")
    expected = CommandResult(
        exit_status=0,
        signal=None,
        timed_out=False,
        stdout_bytes=3,
        stderr_bytes=17,
        elapsed_ns=42,
        output_path=output,
        bounded_tail="fixture diagnostic",
    )
    calls: list[list[str]] = []

    def execute(argv: list[str], path: Path | None) -> CommandResult:
        calls.append(argv)
        assert path == output
        return expected

    runner = make_runner(root, execute, validate_runtime=True)
    argv = [str(FIXED_BINARIES["gwinfo"]), "--help"]
    assert runner.command_result(argv, output) == expected
    assert calls == [argv]
    expect_harness_error(
        lambda: runner.command_result(["/bin/sh", "-c", "true"]),
        "live runner rejected a non-fixed executable",
    )
    assert calls == [argv]

    signal_result = CommandResult(
        exit_status=None,
        signal=15,
        timed_out=False,
        stdout_bytes=0,
        stderr_bytes=0,
        elapsed_ns=10,
        output_path=None,
        bounded_tail="",
    )
    runner.execute = lambda _argv, _output: signal_result
    expect_harness_error(
        lambda: runner.command([str(FIXED_BINARIES["gwinfo"])]),
        "fixed command failed: gwinfo",
    )


def test_snapshot_busy_then_success_is_bounded_and_atomic(root: Path) -> None:
    calls = 0
    destination = root / "state.json"
    destination.write_text("previous\n", encoding="utf-8")
    old_inode = destination.stat().st_ino

    def execute(argv: list[str], output: Path | None) -> CommandResult:
        nonlocal calls
        calls += 1
        assert argv[0] == str(FIXED_BINARIES["gwinfo"])
        if calls == 1:
            return query_result(
                output, status=1, stderr_bytes=51,
                tail="gwinfo: output snapshot is temporarily unavailable",
            )
        write_snapshot(output)
        return query_result(output)

    runner = make_runner(root, execute, validate_runtime=True)
    value = runner.snapshot("state.json", "always-eligible", True)

    assert calls == 2
    assert value["vrr"][0]["effective_enabled"] is True
    assert destination.stat().st_ino != old_inode
    assert "previous" not in destination.read_text(encoding="utf-8")
    diagnostic = read_query_diagnostic(root, "state.json")
    assert diagnostic["schema"] == QUERY_DIAGNOSTIC_SCHEMA
    assert diagnostic["outcome"] == "converged"
    assert diagnostic["attempt_count"] == 2
    assert diagnostic["attempt_limit"] == QUERY_ATTEMPTS
    assert len(diagnostic["attempts"]) == 2


def test_snapshot_command_failures_are_typed(root: Path) -> None:
    cases = (
        ("zero", query_result(None, stdout_bytes=0),
         "exited successfully with zero output bytes"),
        ("status", query_result(None, status=2, stderr_bytes=17,
                                tail="permission denied"),
         "exited with status 2: permission denied"),
        ("timeout", query_result(None, status=None, timed_out=True),
         "exceeded its command deadline"),
        ("signal", query_result(None, status=None, terminating_signal=9),
         "terminated by signal 9"),
    )
    for label, result, message in cases:
        case_root = root / label
        case_root.mkdir()
        calls = 0

        def execute(_argv: list[str], output: Path | None,
                    result: CommandResult = result) -> CommandResult:
            nonlocal calls
            calls += 1
            return CommandResult(
                result.exit_status, result.signal, result.timed_out,
                result.stdout_bytes, result.stderr_bytes, result.elapsed_ns,
                output, result.bounded_tail,
            )

        runner = make_runner(case_root, execute, validate_runtime=True)
        expect_harness_error_contains(
            lambda: runner.snapshot("state.json", "always-eligible", True),
            message,
        )
        assert calls == 1
        diagnostic = read_query_diagnostic(case_root, "state.json")
        assert diagnostic["outcome"] == "fatal"
        assert diagnostic["attempt_count"] == 1
        assert not (case_root / ".state.json.query.tmp").exists()


def test_snapshot_rejects_malformed_json_without_retry(root: Path) -> None:
    calls = 0

    def execute(_argv: list[str], output: Path | None) -> CommandResult:
        nonlocal calls
        calls += 1
        assert output is not None
        output.write_text("{", encoding="utf-8")
        return query_result(output)

    runner = make_runner(root, execute, validate_runtime=True)
    expect_harness_error_contains(
        lambda: runner.snapshot("state.json", "always-eligible", True),
        "query returned malformed JSON",
    )
    assert calls == 1
    diagnostic = read_query_diagnostic(root, "state.json")
    assert diagnostic["attempts"][0]["classification"] == "malformed-json"
    assert not (root / ".state.json.query.tmp").exists()


def test_snapshot_preserves_last_coherent_state_and_caps_queries(
        root: Path) -> None:
    calls = 0
    destination = root / "state.json"
    destination.write_text("previous\n", encoding="utf-8")

    def execute(_argv: list[str], output: Path | None) -> CommandResult:
        nonlocal calls
        calls += 1
        write_snapshot(output, "off", False)
        return query_result(output)

    runner = make_runner(root, execute, validate_runtime=True)
    expect_harness_error_contains(
        lambda: runner.snapshot("state.json", "always-eligible", True),
        f"after {QUERY_ATTEMPTS} queries",
    )

    assert calls == QUERY_ATTEMPTS
    assert calls < 200
    assert destination.read_text(encoding="utf-8") == "previous\n"
    diagnostic = read_query_diagnostic(root, "state.json")
    assert diagnostic["outcome"] == "exhausted"
    assert diagnostic["attempt_count"] == QUERY_ATTEMPTS
    coherent_path = Path(str(diagnostic["last_coherent_json"]))
    assert coherent_path.is_file()
    assert json.loads(
        coherent_path.read_text(encoding="utf-8"),
    )["vrr"][0]["policy"] == "off"
    assert not (root / ".state.json.query.tmp").exists()


def test_query_diagnostic_tail_is_bounded(root: Path) -> None:
    tail = "x" * (COMMAND_TAIL_BYTES * 4)

    def execute(_argv: list[str], output: Path | None) -> CommandResult:
        return query_result(
            output, status=3, stderr_bytes=len(tail), tail=tail,
        )

    runner = make_runner(root, execute, validate_runtime=True)
    expect_harness_error_contains(
        lambda: runner.snapshot("state.json", "always-eligible", True),
        "exited with status 3",
    )
    diagnostic = read_query_diagnostic(root, "state.json")
    recorded = diagnostic["attempts"][0]["command"]["bounded_tail"]
    assert len(recorded.encode("utf-8")) <= COMMAND_TAIL_BYTES
    diagnostic_path = (
        root / ".query-diagnostics" / "state.json.diagnostic.json"
    )
    assert diagnostic_path.stat().st_size <= 32 * 1024
    assert diagnostic_path.stat().st_mode & 0o077 == 0


def test_policy_cleanup_uses_bounded_typed_queries(root: Path) -> None:
    calls = 0

    def execute(_argv: list[str], output: Path | None) -> CommandResult:
        nonlocal calls
        calls += 1
        assert output is not None
        windows = [{"window": 41}] if calls == 1 else []
        candidate = 41 if calls == 2 else 0
        output.write_text(
            json.dumps({
                "vrr": [{"name": "DP-1", "candidate_window": candidate}],
                "windows": windows,
            }) + "\n",
            encoding="utf-8",
        )
        return query_result(output)

    runner = make_runner(root, execute, validate_runtime=True)
    runner.wait_policy_cleanup()

    assert calls == 3
    assert calls < 400
    assert runner.cleanup_wait_count == 1
    assert not (root / ".policy-cleanup.query.tmp").exists()
    diagnostic = read_query_diagnostic(root, "policy-cleanup")
    assert diagnostic["outcome"] == "converged"
    assert diagnostic["attempt_count"] == 3
    assert diagnostic["attempt_limit"] == CLEANUP_QUERY_ATTEMPTS
    coherent_path = Path(str(diagnostic["last_coherent_json"]))
    assert json.loads(coherent_path.read_text(
        encoding="utf-8",
    ))["vrr"][0]["candidate_window"] == 41


def test_policy_cleanup_fails_once_on_signal(root: Path) -> None:
    calls = 0

    def execute(_argv: list[str], output: Path | None) -> CommandResult:
        nonlocal calls
        calls += 1
        return query_result(
            output, status=None, terminating_signal=15,
        )

    runner = make_runner(root, execute, validate_runtime=True)
    expect_harness_error_contains(
        runner.wait_policy_cleanup,
        "coordinated client cleanup query failed: "
        "gwinfo query terminated by signal 15",
    )
    assert calls == 1
    assert not (root / ".policy-cleanup.query.tmp").exists()
    diagnostic = read_query_diagnostic(root, "policy-cleanup")
    assert diagnostic["outcome"] == "fatal"
    assert diagnostic["attempt_count"] == 1


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


def test_client_result_uses_bounded_live_deadline(root: Path) -> None:
    calls: list[tuple[list[str], Path | None]] = []
    waits: list[tuple[Path, str, int]] = []

    def execute(argv: list[str], output: Path | None) -> int:
        calls.append((argv, output))
        return 0

    runner = make_runner(root, execute)
    runner.wait_path = lambda path, kind, attempts: waits.append(
        (path, kind, attempts),
    )
    result = runner.start_client("off-cadence", "cadence", "default", True)

    assert result == root / "client-off-cadence.json"
    assert waits == [(result, "file", CLIENT_RESULT_WAIT_ATTEMPTS)]
    launches = [argv for argv, _ in calls
                if launched_unit(argv) ==
                "m14-hardware-client-off-cadence.service"]
    assert len(launches) == 1
    assert "--frames" in launches[0]
    assert "180" in launches[0]
    assert launches[0][launches[0].index("--control-socket") + 1] == \
        "/run/glasswyrm-m14-hardware/control.sock"
    assert launches[0][launches[0].index("--output") + 1] == "DP-1"


def test_cadence_client_requires_v3_presentation_state(root: Path) -> None:
    state = {
        "schema": "glasswyrm.m14-vrr-client.v3",
        "mode": "cadence",
        "preference": "Default",
        "selected_output": "DP-1",
        "presentation_paced": True,
        "scheduled_frame_count": 180,
        "submitted_frame_count": 180,
        "presented_frame_count": 121,
        "maximum_outstanding_updates": 1,
    }

    def execute(_argv: list[str], _output: Path | None) -> int:
        return 0

    def publish(path: Path, kind: str, attempts: int) -> None:
        assert kind == "file" and attempts == CLIENT_RESULT_WAIT_ATTEMPTS
        path.write_text(json.dumps(state) + "\n", encoding="utf-8")

    runner = make_runner(root, execute, validate_runtime=True)
    runner.wait_path = publish
    runner.start_client("on-cadence", "cadence", "default", True)

    state["presented_frame_count"] = 120
    expect_harness_error(
        lambda: runner.start_client(
            "off-cadence", "cadence", "default", True,
        ),
        "client off-cadence omitted presentation-paced state",
    )


def test_client_result_timeout_reports_unit_and_log(root: Path) -> None:
    def execute(argv: list[str], output: Path | None) -> int:
        if is_systemctl(argv, "show"):
            write_unit_state(output, "loaded", "active")
        return 0

    runner = make_runner(root, execute)

    def timed_out(path: Path, kind: str, attempts: int) -> None:
        assert attempts == CLIENT_RESULT_WAIT_ATTEMPTS
        raise HarnessError(f"timed out waiting for {kind}: {path}")

    runner.wait_path = timed_out
    expect_harness_error(
        lambda: runner.start_client(
            "off-cadence", "cadence", "default", True,
        ),
        "client off-cadence did not publish its bounded result "
        f"(loaded/active); inspect {root / 'm14-hardware-client-off-cadence.log'}",
    )


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

        command_result = root / "command-result"
        command_result.mkdir()
        test_command_result_contract(command_result)

        snapshot_busy = root / "snapshot-busy"
        snapshot_busy.mkdir()
        test_snapshot_busy_then_success_is_bounded_and_atomic(snapshot_busy)

        snapshot_failures = root / "snapshot-failures"
        snapshot_failures.mkdir()
        test_snapshot_command_failures_are_typed(snapshot_failures)

        snapshot_malformed = root / "snapshot-malformed"
        snapshot_malformed.mkdir()
        test_snapshot_rejects_malformed_json_without_retry(snapshot_malformed)

        snapshot_pending = root / "snapshot-pending"
        snapshot_pending.mkdir()
        test_snapshot_preserves_last_coherent_state_and_caps_queries(
            snapshot_pending,
        )

        snapshot_tail = root / "snapshot-tail"
        snapshot_tail.mkdir()
        test_query_diagnostic_tail_is_bounded(snapshot_tail)

        cleanup_queries = root / "cleanup-queries"
        cleanup_queries.mkdir()
        test_policy_cleanup_uses_bounded_typed_queries(cleanup_queries)

        cleanup_signal = root / "cleanup-signal"
        cleanup_signal.mkdir()
        test_policy_cleanup_fails_once_on_signal(cleanup_signal)

        client_wait = root / "client-wait"
        client_wait.mkdir()
        test_client_result_uses_bounded_live_deadline(client_wait)

        client_timeout = root / "client-timeout"
        client_timeout.mkdir()
        test_client_result_timeout_reports_unit_and_log(client_timeout)

        client_presentation = root / "client-presentation"
        client_presentation.mkdir()
        test_cadence_client_requires_v3_presentation_state(client_presentation)

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
