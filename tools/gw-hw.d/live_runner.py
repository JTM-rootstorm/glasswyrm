"""Fail-closed, shell-free physical session orchestration."""

from __future__ import annotations

import fcntl
import json
import os
from pathlib import Path
import signal
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import asdict, dataclass, replace
from typing import Any

from common import (
    BUILD_ROOT, HarnessError, MAX_JSON_BYTES, MIN_ENABLED_INTERVALS, RUN_STEPS,
    TTY_PATTERN, _read_json, _read_regular, _write_json,
    vrr_rejection_reasons,
)
from provenance import PROBE_BINARY, PROVENANCE_BINARIES
from evidence import drm_evidence_streams, sealed_vrr_records

RUNTIME_ROOT = Path("/run/glasswyrm-m14-hardware")
LIVE_HARNESS_SCOPE = "glasswyrm-m14-harness.scope"
FIXED_BINARIES = {
    **PROVENANCE_BINARIES,
    "nvidia-drm-vrr-probe": PROBE_BINARY,
    "systemctl": Path("/usr/bin/systemctl"),
    "systemd-run": Path("/usr/bin/systemd-run"),
    "chvt": Path("/usr/bin/chvt"),
}

LIVE_UNITS = {
    "gwm": "gwm-m14-hardware.service",
    "gwcomp": "gwcomp-m14-hardware.service",
    "server": "glasswyrmd-m14-hardware.service",
}

LIVE_CLIENT_TAGS = (
    "off-cadence", "on-cadence", "borderless", "focus-a", "focus-b",
    "app-default", "app-prefer", "app-preferences", "always",
)

LIVE_MANAGED_UNITS = (
    *LIVE_UNITS.values(),
    *(f"m14-hardware-client-{tag}.service" for tag in LIVE_CLIENT_TAGS),
)

FULL_ACCEPTANCE_STAGES = (
    "start-stack",
    "stack-cadence",
    "policy-matrix",
    "vt-cycle",
    "restart-gwm",
    "restart-gwcomp",
    "pixel-parity",
    "shutdown-and-restore",
)
DIAGNOSTIC_STACK_STAGES = (
    "stack-cadence",
    "policy-matrix",
    "vt-cycle",
    "restart-gwm",
    "restart-gwcomp",
    "pixel-parity",
)

PATH_WAIT_ATTEMPTS = 200
CLIENT_RESULT_WAIT_ATTEMPTS = 1200
COMMAND_TIMEOUT_SECONDS = 120
COMMAND_TAIL_BYTES = 2048
QUERY_ATTEMPTS = 3
CLEANUP_QUERY_ATTEMPTS = 5
QUERY_SLOW_NS = 1_000_000_000
COMMAND_DIAGNOSTIC_SCHEMA = "glasswyrm.m14-command-diagnostic.v1"
QUERY_DIAGNOSTIC_SCHEMA = "glasswyrm.m14-query-diagnostic.v1"
RETRYABLE_QUERY_TAILS = (
    "output snapshot is temporarily unavailable",
    "timed out waiting for output snapshot readiness",
)


@dataclass(frozen=True)
class CommandResult:
    """Bounded evidence from one shell-free fixed executable invocation."""

    exit_status: int | None
    signal: int | None
    timed_out: bool
    stdout_bytes: int
    stderr_bytes: int
    elapsed_ns: int
    output_path: Path | None
    bounded_tail: str

    @property
    def succeeded(self) -> bool:
        return (not self.timed_out and self.signal is None and
                self.exit_status == 0)


def _control_group_has_live_scope(contents: str) -> bool:
    """Return whether the process belongs to the fixed detached live scope."""
    for line in contents.splitlines():
        fields = line.split(":", 2)
        if len(fields) == 3 and LIVE_HARNESS_SCOPE in Path(fields[2]).parts:
            return True
    return False


def require_live_harness_scope(
        path: Path = Path("/proc/self/cgroup")) -> None:
    try:
        contents = path.read_text(encoding="ascii")
    except (OSError, UnicodeError) as error:
        raise HarnessError(
            "live run cannot verify its detached systemd scope") from error
    if not _control_group_has_live_scope(contents):
        raise HarnessError(
            "live run requires systemd-run --scope "
            "--unit=glasswyrm-m14-harness")


class FixedLiveRunner:
    """Shell-free, fixed-argv physical runner with unconditional cleanup."""

    def __init__(self, config: dict[str, object], artifacts: Path,
                 execute: Any = None, active_tty: str | None = None,
                 verify_paths: bool = True, ready: Any = None,
                 state_reader: Any = None, validate_runtime: bool = True,
                 preflight_reader: Any = None,
                 detached_invocation: bool = False) -> None:
        self.config = config
        self.artifacts = artifacts
        self.execute = execute or self._execute
        self.active_tty = active_tty
        self.verify_paths = verify_paths
        self.ready = ready or self._ready
        self.state_reader = state_reader or self._console_state
        self.preflight_reader = preflight_reader or self._live_console_preflight
        self.recheck_console = verify_paths or preflight_reader is not None
        self.validate_runtime = validate_runtime
        self.detached_invocation = detached_invocation
        self.steps: list[str] = []
        self.cleanup_attempted = False
        self.cleanup_errors: list[str] = []
        self.getty_stopped = False
        self.getty_was_active = False
        tty_number = TTY_PATTERN.fullmatch(str(config["tty"])).group(1)  # type: ignore[union-attr]
        self.getty_unit = f"getty@tty{tty_number}.service"
        self.alternate_tty = TTY_PATTERN.fullmatch(
            str(config["alternate_tty"])).group(1)  # type: ignore[union-attr]
        self.managed_units: list[str] = []
        self.before_state: dict[str, object] | None = None
        self.after_state: dict[str, object] | None = None
        self.restoration_evidence: dict[str, object] | None = None
        self.snapshot_expectations: list[dict[str, object]] = []
        self.cleanup_wait_count = 0
        self.cadence_ranges: dict[str, tuple[int, int]] = {}
        self._cadence_starts: dict[str, int] = {}
        self._command_failure_count = 0
        self.record_full_steps = True

    @staticmethod
    def _execute(argv: list[str], output: Path | None = None) -> CommandResult:
        allowed = {str(path) for path in FIXED_BINARIES.values()}
        if not argv or argv[0] not in allowed:
            raise HarnessError("live runner rejected a non-fixed executable")
        started = time.monotonic_ns()
        stream = output.open("a+b") if output else tempfile.TemporaryFile()
        stderr = tempfile.TemporaryFile()
        stdout_start = stream.tell()
        exit_status: int | None = None
        terminating_signal: int | None = None
        timed_out = False
        try:
            try:
                result = subprocess.run(
                    argv, check=False, stdin=subprocess.DEVNULL,
                    stdout=stream, stderr=stderr,
                    timeout=COMMAND_TIMEOUT_SECONDS,
                )
                if result.returncode < 0:
                    terminating_signal = -result.returncode
                else:
                    exit_status = result.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
            stream.flush()
            stderr.flush()
            stdout_bytes = stream.tell() - stdout_start
            stderr_bytes = stderr.tell()
            tails: list[bytes] = []
            if stdout_bytes:
                stream.seek(max(stdout_start, stream.tell() - COMMAND_TAIL_BYTES))
                tails.append(stream.read(COMMAND_TAIL_BYTES))
            if stderr_bytes:
                stderr.seek(max(0, stderr_bytes - COMMAND_TAIL_BYTES))
                tails.append(stderr.read(COMMAND_TAIL_BYTES))
            tail = b"\n".join(tails)[-COMMAND_TAIL_BYTES:].decode(
                "utf-8", errors="replace",
            )
            return CommandResult(
                exit_status, terminating_signal, timed_out,
                stdout_bytes, stderr_bytes,
                time.monotonic_ns() - started, output, tail,
            )
        finally:
            stream.close()
            stderr.close()

    def command_result(self, argv: list[str],
                       output: Path | None = None) -> CommandResult:
        """Run one fixed argv and normalize legacy injected test executors."""
        allowed = {str(path) for path in FIXED_BINARIES.values()}
        if not argv or argv[0] not in allowed:
            raise HarnessError("live runner rejected a non-fixed executable")
        started = time.monotonic_ns()
        raw = self.execute(argv, output)
        if isinstance(raw, CommandResult):
            tail = raw.bounded_tail.encode("utf-8", errors="replace")
            if len(tail) > COMMAND_TAIL_BYTES:
                tail = tail[-COMMAND_TAIL_BYTES:]
                while tail and tail[0] & 0xC0 == 0x80:
                    tail = tail[1:]
            return replace(
                raw,
                output_path=output,
                bounded_tail=tail.decode("utf-8", errors="replace"),
            )
        if isinstance(raw, bool) or not isinstance(raw, int):
            raise HarnessError("live runner executor returned an invalid result")
        size = 0
        tail = ""
        if output is not None and output.is_file():
            contents = _read_regular(output, MAX_JSON_BYTES)
            size = len(contents)
            tail = contents[-COMMAND_TAIL_BYTES:].decode(
                "utf-8", errors="replace",
            )
        return CommandResult(
            raw if raw >= 0 else None,
            -raw if raw < 0 else None,
            False,
            size,
            0,
            time.monotonic_ns() - started,
            output,
            tail,
        )

    def command(self, argv: list[str], output: str | None = None) -> None:
        result = self.command_result(
            argv, self.artifacts / output if output else None,
        )
        if not result.succeeded:
            self._write_command_diagnostic(argv, result)
            raise HarnessError(f"fixed command failed: {Path(argv[0]).name}")

    @staticmethod
    def _command_evidence(result: CommandResult) -> dict[str, object]:
        value = asdict(result)
        value["output_path"] = (str(result.output_path)
                                if result.output_path is not None else None)
        return value

    @staticmethod
    def _diagnostic_safe_name(name: str) -> str:
        return "".join(
            character if character.isalnum() or character in ".-_" else "_"
            for character in name
        )

    def _command_diagnostic_path(self, executable: str) -> Path:
        directory = self.artifacts / ".command-diagnostics"
        if directory.exists():
            status = directory.lstat()
            if (not stat.S_ISDIR(status.st_mode) or status.st_mode & 0o077):
                raise HarnessError(
                    "command diagnostics path must be a private directory")
        else:
            directory.mkdir(mode=0o700)
        self._command_failure_count += 1
        safe_name = self._diagnostic_safe_name(Path(executable).name)
        return directory / (
            f"{self._command_failure_count:03d}-{safe_name}.diagnostic.json"
        )

    def _write_command_diagnostic(
            self, argv: list[str], result: CommandResult) -> Path:
        destination = self._command_diagnostic_path(argv[0])
        temporary = destination.with_name(f".{destination.name}.tmp")
        payload = {
            "schema": COMMAND_DIAGNOSTIC_SCHEMA,
            "executable": Path(argv[0]).name,
            "argv": argv,
            "command": self._command_evidence(result),
        }
        encoded = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode(
            "utf-8",
        )
        if len(encoded) > 32 * 1024:
            raise HarnessError("command diagnostic exceeded its bounded schema")
        temporary.unlink(missing_ok=True)
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC |
            os.O_NOFOLLOW,
            0o600,
        )
        try:
            offset = 0
            while offset < len(encoded):
                offset += os.write(descriptor, encoded[offset:])
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        os.replace(temporary, destination)
        return destination

    def _query_diagnostic_path(self, name: str) -> Path:
        directory = self.artifacts / ".query-diagnostics"
        if directory.exists():
            status = directory.lstat()
            if (not stat.S_ISDIR(status.st_mode) or status.st_mode & 0o077):
                raise HarnessError(
                    "query diagnostics path must be a private directory")
        else:
            directory.mkdir(mode=0o700)
        return directory / (
            f"{self._diagnostic_safe_name(name)}.diagnostic.json"
        )

    def _write_query_diagnostic(
            self, name: str, outcome: str, attempts: list[dict[str, object]],
            last_error: str,
            last_coherent: Path | None = None,
            attempt_limit: int = QUERY_ATTEMPTS) -> Path:
        destination = self._query_diagnostic_path(name)
        temporary = destination.with_name(f".{destination.name}.tmp")
        payload = {
            "schema": QUERY_DIAGNOSTIC_SCHEMA,
            "query": name,
            "outcome": outcome,
            "attempt_count": len(attempts),
            "attempt_limit": attempt_limit,
            "last_error": last_error,
            "last_coherent_json": (str(last_coherent)
                                   if last_coherent is not None else None),
            "attempts": attempts,
        }
        encoded = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode(
            "utf-8",
        )
        if len(encoded) > 32 * 1024:
            raise HarnessError("query diagnostic exceeded its bounded schema")
        temporary.unlink(missing_ok=True)
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC |
            os.O_NOFOLLOW,
            0o600,
        )
        try:
            offset = 0
            while offset < len(encoded):
                offset += os.write(descriptor, encoded[offset:])
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        os.replace(temporary, destination)
        return destination

    @staticmethod
    def _query_failure(result: CommandResult) -> tuple[str, bool] | None:
        if result.timed_out:
            return "gwinfo query exceeded its command deadline", False
        if result.signal is not None:
            return f"gwinfo query terminated by signal {result.signal}", False
        if result.exit_status != 0:
            detail = result.bounded_tail.strip()
            message = f"gwinfo query exited with status {result.exit_status}"
            if detail:
                message += f": {detail}"
            retryable = any(marker in detail for marker in RETRYABLE_QUERY_TAILS)
            return message, retryable
        if result.stdout_bytes == 0:
            return "gwinfo query exited successfully with zero output bytes", False
        return None

    def _bounded_json_query(
            self, name: str, description: str, argv: list[str],
            temporary: Path, attempt_limit: int,
            validate: Callable[[dict[str, Any]], None],
            destination: Path | None = None) -> dict[str, Any]:
        last_error = "state was not observed"
        attempts: list[dict[str, object]] = []
        last_coherent: Path | None = None
        for attempt in range(attempt_limit):
            temporary.unlink(missing_ok=True)
            result = self.command_result(argv, temporary)
            record: dict[str, object] = {
                "attempt": attempt + 1,
                "command": self._command_evidence(result),
            }
            failure = self._query_failure(result)
            if failure is not None:
                last_error, retryable = failure
                record["classification"] = (
                    "retryable-command-failure" if retryable
                    else "fatal-command-failure"
                )
                record["error"] = last_error
                attempts.append(record)
                self._write_query_diagnostic(
                    name, "retrying" if retryable else "fatal",
                    attempts, last_error, last_coherent, attempt_limit,
                )
                temporary.unlink(missing_ok=True)
                if retryable and attempt + 1 < attempt_limit:
                    time.sleep(.05)
                    continue
                raise HarnessError(
                    f"{description} query failed: {last_error}")
            try:
                value = _read_json(temporary)
            except HarnessError as error:
                last_error = str(error)
                record["classification"] = "malformed-json"
                record["error"] = last_error
                attempts.append(record)
                self._write_query_diagnostic(
                    name, "fatal", attempts, last_error, last_coherent,
                    attempt_limit,
                )
                temporary.unlink(missing_ok=True)
                raise HarnessError(
                    f"{description} query returned malformed JSON: "
                    f"{last_error}") from error
            try:
                validate(value)
            except HarnessError as error:
                last_error = str(error)
                record["classification"] = "coherent-state-pending"
                record["error"] = last_error
                attempts.append(record)
                last_coherent = self._query_diagnostic_path(name).with_name(
                    f"{self._diagnostic_safe_name(name)}.last-coherent.json",
                )
                os.replace(temporary, last_coherent)
                self._write_query_diagnostic(
                    name,
                    ("retrying" if attempt + 1 < attempt_limit
                     else "exhausted"),
                    attempts, last_error, last_coherent, attempt_limit,
                )
                if attempt + 1 < attempt_limit:
                    time.sleep(.05)
                continue
            if destination is None:
                temporary.unlink(missing_ok=True)
            else:
                os.replace(temporary, destination)
            if attempts or result.elapsed_ns >= QUERY_SLOW_NS:
                record["classification"] = (
                    "slow-success" if result.elapsed_ns >= QUERY_SLOW_NS
                    else "success"
                )
                attempts.append(record)
                self._write_query_diagnostic(
                    name, "converged", attempts, "", last_coherent,
                    attempt_limit,
                )
            return value
        temporary.unlink(missing_ok=True)
        raise HarnessError(
            f"{description} query did not converge after "
            f"{attempt_limit} queries: {last_error}")

    @staticmethod
    def _ready(path: Path, kind: str) -> bool:
        if kind == "socket":
            try:
                return stat.S_ISSOCK(path.stat().st_mode)
            except OSError:
                return False
        return path.is_file() and path.stat().st_size > 0

    def wait_path(self, path: Path, kind: str = "socket",
                  attempts: int = PATH_WAIT_ATTEMPTS) -> None:
        for _ in range(attempts):
            if self.ready(path, kind):
                return
            time.sleep(.05)
        raise HarnessError(f"timed out waiting for {kind}: {path}")

    def wait_absent(self, path: Path) -> None:
        if not self.validate_runtime:
            return
        for _ in range(200):
            if not path.exists():
                return
            time.sleep(.05)
        raise HarnessError(f"timed out waiting for removal: {path}")

    def wait_replaced(self, path: Path, old_inode: int) -> None:
        if not self.validate_runtime:
            return
        for _ in range(200):
            try:
                if path.stat().st_ino != old_inode:
                    return
            except OSError:
                pass
            time.sleep(.05)
        raise HarnessError(f"timed out waiting for socket replacement: {path}")

    def _console_state(self) -> dict[str, object]:
        tty = str(self.config["tty"])
        active_vt = 0
        kd_mode = -1
        try:
            with open("/dev/tty0", "rb", buffering=0) as control:
                packed = fcntl.ioctl(control.fileno(), 0x5603, bytes(6))
                active_vt = struct.unpack("HHH", packed)[0]
            with open(tty, "rb", buffering=0) as terminal:
                packed = fcntl.ioctl(terminal.fileno(), 0x4B3B, bytes(4))
                kd_mode = struct.unpack("I", packed)[0]
        except OSError as error:
            raise HarnessError(f"cannot capture exact VT/KD state: {error}") from error
        getty_active = self.command_result(
            [str(FIXED_BINARIES["systemctl"]), "is-active", self.getty_unit],
        ).succeeded
        return {"active_vt": active_vt, "kd_mode": kd_mode,
                "getty_active": getty_active}

    def _live_console_preflight(self) -> dict[str, object]:
        try:
            active_name = Path("/sys/class/tty/tty0/active").read_text(
                encoding="ascii").strip()
            active_tty = f"/dev/{active_name}"
            kd_modes: dict[str, int] = {}
            for tty in (str(self.config["tty"]),
                        str(self.config["alternate_tty"])):
                with open(tty, "rb", buffering=0) as terminal:
                    packed = fcntl.ioctl(terminal.fileno(), 0x4B3B, bytes(4))
                    kd_modes[tty] = struct.unpack("I", packed)[0]
        except (OSError, UnicodeError) as error:
            raise HarnessError(
                f"cannot recheck exact active VT/KD_TEXT state: {error}") from error
        return {"active_tty": active_tty, "kd_modes": kd_modes}

    def verify_live_console(self) -> None:
        if not self.recheck_console:
            return
        state = self.preflight_reader()
        active_tty = state.get("active_tty")
        modes = state.get("kd_modes")
        if active_tty != self.config["tty"]:
            raise HarnessError(
                "live run active VT changed before takeover: "
                f"expected {self.config['tty']}, observed {active_tty}")
        if not isinstance(modes, dict):
            raise HarnessError("live console preflight omitted exact KD modes")
        for tty in (str(self.config["tty"]),
                    str(self.config["alternate_tty"])):
            if modes.get(tty) != 0:
                raise HarnessError(
                    f"live run requires KD_TEXT on {tty}; observed KD mode "
                    f"{modes.get(tty)}")

    def unit_state(self, name: str) -> tuple[str, str]:
        path = self.artifacts / ".systemd-unit-state.tmp"
        path.unlink(missing_ok=True)
        argv = [str(FIXED_BINARIES["systemctl"]), "show",
                "--property=LoadState", "--property=ActiveState", name]
        try:
            if not self.command_result(argv, path).succeeded:
                raise HarnessError(
                    f"cannot inspect fixed transient unit: {name}")
            fields: dict[str, str] = {}
            for line in _read_regular(path, 4096).decode("ascii").splitlines():
                key, separator, value = line.partition("=")
                if not separator or key in fields:
                    raise HarnessError(
                        f"fixed transient unit returned malformed state: {name}")
                fields[key] = value
            if set(fields) != {"LoadState", "ActiveState"}:
                raise HarnessError(
                    f"fixed transient unit omitted exact state: {name}")
            return fields["LoadState"], fields["ActiveState"]
        except UnicodeError as error:
            raise HarnessError(
                f"fixed transient unit returned non-ASCII state: {name}") from error
        finally:
            path.unlink(missing_ok=True)

    def prepare_unit_names(self) -> None:
        for name in LIVE_MANAGED_UNITS:
            load_state, active_state = self.unit_state(name)
            if load_state == "not-found" and active_state == "inactive":
                continue
            if (load_state == "loaded" and
                    active_state in {"inactive", "failed"}):
                self.command([str(FIXED_BINARIES["systemctl"]),
                              "reset-failed", name])
                for _ in range(20):
                    load_state, active_state = self.unit_state(name)
                    if (load_state == "not-found" and
                            active_state == "inactive"):
                        break
                    time.sleep(.05)
                else:
                    raise HarnessError(
                        f"stale fixed transient unit did not unload: {name}")
                continue
            raise HarnessError(
                f"fixed transient unit name is already in use: {name} "
                f"({load_state}/{active_state})")

    def stop_unit(self, name: str) -> None:
        argv = [str(FIXED_BINARIES["systemctl"]), "stop", name]
        stop_result = self.command_result(argv)
        if self.verify_paths:
            reset_attempted = False
            for _ in range(20):
                load_state, active_state = self.unit_state(name)
                if load_state == "not-found" and active_state == "inactive":
                    break
                if (load_state == "loaded" and
                        active_state in {"inactive", "failed"} and
                        not reset_attempted):
                    if not self.command_result([
                            str(FIXED_BINARIES["systemctl"]),
                            "reset-failed", name]).succeeded:
                        raise HarnessError(
                            f"fixed transient unit could not be reset: {name}")
                    reset_attempted = True
                elif active_state not in {
                        "active", "activating", "deactivating", "inactive",
                        "failed"}:
                    raise HarnessError(
                        f"fixed transient unit returned unsafe state: {name}")
                time.sleep(.05)
            else:
                raise HarnessError(
                    f"fixed transient unit did not unload after stop: {name} "
                    f"(stop={stop_result.exit_status}, "
                    f"{load_state}/{active_state})")
        elif not stop_result.succeeded:
            raise HarnessError(f"fixed transient unit did not stop: {name}")
        self.managed_units = [unit for unit in self.managed_units
                              if unit != name]

    def start_unit(self, name: str, executable: str, arguments: list[str],
                   properties: list[str] | None = None) -> None:
        unit_name = name.removesuffix(".service")
        unit_log = self.artifacts / f"{unit_name}.log"
        argv = [str(FIXED_BINARIES["systemd-run"]), f"--unit={unit_name}",
                "--collect", "--quiet", "--property=Type=exec"]
        for value in properties or []:
            argv.append(f"--property={value}")
        argv += [f"--property=StandardOutput=append:{unit_log}",
                 f"--property=StandardError=append:{unit_log}"]
        argv += ["--", str(FIXED_BINARIES[executable]), *arguments]
        self.command(argv, f"{unit_name}.log")
        self.managed_units.append(name)

    def set_policy(self, policy: str) -> None:
        self.command([str(FIXED_BINARIES["gwout"]), "--socket",
                      str(RUNTIME_ROOT / "control.sock"), "set",
                      str(self.config["connector"]), "--vrr", policy, "--json"])

    def start_client(self, tag: str, mode: str, preference: str | None = None,
                     cadence: bool = False, repaint: bool = False) -> Path:
        unit = f"m14-hardware-client-{tag}.service"
        result = self.artifacts / f"client-{tag}.json"
        hold_ms = "60000" if repaint else "30000"
        arguments = ["--display", ":14", "--mode", mode, "--result", str(result),
                     "--hold-ms", hold_ms]
        if cadence:
            arguments += ["--frames", "180", "--target-refresh-hz",
                          str(self.config["target_refresh_hz"]),
                          "--control-socket",
                          str(RUNTIME_ROOT / "control.sock"),
                          "--output", str(self.config["connector"])]
        if preference is not None:
            arguments += ["--preference", preference]
        if repaint:
            arguments += ["--repaint-trigger",
                          str(RUNTIME_ROOT / "repaint.request"),
                          "--repaint-count", "3"]
        self.start_unit(unit, "client", arguments,
                        ["KillMode=mixed", "SuccessExitStatus=143"])
        try:
            self.wait_path(result, "file", CLIENT_RESULT_WAIT_ATTEMPTS)
        except HarnessError as error:
            load_state, active_state = self.unit_state(unit)
            raise HarnessError(
                f"client {tag} did not publish its bounded result "
                f"({load_state}/{active_state}); inspect "
                f"{self.artifacts / unit.removesuffix('.service')}.log") from error
        if self.validate_runtime:
            state = _read_json(result)
            if (state.get("schema") != "glasswyrm.m14-vrr-client.v3" or
                    state.get("mode") != mode or (preference is not None and
                    state.get("preference", "").lower() != preference)):
                raise HarnessError(f"client {tag} published unexpected state")
            if cadence and (
                    state.get("selected_output") != self.config["connector"] or
                    state.get("presentation_paced") is not True or
                    state.get("scheduled_frame_count") != 180 or
                    state.get("submitted_frame_count") != 180 or
                    not isinstance(state.get("presented_frame_count"), int) or
                    state["presented_frame_count"] < MIN_ENABLED_INTERVALS + 1 or
                    state["presented_frame_count"] > 180 or
                    state.get("maximum_outstanding_updates") != 1):
                raise HarnessError(
                    f"client {tag} omitted presentation-paced state")
        return result

    def begin_cadence(self, tag: str) -> None:
        if not self.validate_runtime:
            return
        if tag in self._cadence_starts or tag in self.cadence_ranges:
            raise HarnessError(f"cadence scenario {tag} was started more than once")
        report = self.artifacts / "vrr-part-1.jsonl"
        self._cadence_starts[tag] = len(_read_regular(report, MAX_JSON_BYTES))

    def finish_cadence(self, tag: str, enabled: bool) -> None:
        if not self.validate_runtime:
            return
        start = self._cadence_starts.pop(tag, None)
        if start is None:
            raise HarnessError(f"cadence scenario {tag} has no start boundary")
        report = self.artifacts / "vrr-part-1.jsonl"
        last_count = 0
        for _ in range(200):
            contents = _read_regular(report, MAX_JSON_BYTES)
            if start > len(contents):
                raise HarnessError(f"cadence scenario {tag} report was truncated")
            relative_end = contents[start:].rfind(b"\n") + 1
            if relative_end == 0:
                time.sleep(.05)
                continue
            end = start + relative_end
            count = 0
            parsed: list[dict[str, Any]] = []
            for number, line in enumerate(
                    contents[start:end].decode("utf-8").splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    raise HarnessError(
                        f"invalid {tag} cadence JSONL line {number}: {error}") from error
                if not isinstance(record, dict):
                    raise HarnessError(
                        f"invalid {tag} cadence record at line {number}")
                parsed.append(record)
            for record in sealed_vrr_records(parsed, require_seals=True):
                if (record.get("record") in {"vrr-timing", "timing"} and
                        record.get("effective_enabled") is enabled):
                    count += 1
            last_count = count
            if count >= MIN_ENABLED_INTERVALS + 1:
                self.cadence_ranges[tag] = (start, end)
                return
            time.sleep(.05)
        raise HarnessError(
            f"cadence scenario {tag} produced only {last_count} bounded timing records")

    def stop_client(self, tag: str) -> None:
        unit = f"m14-hardware-client-{tag}.service"
        self.stop_unit(unit)

    def wait_policy_cleanup(self) -> None:
        self.cleanup_wait_count += 1
        if not self.validate_runtime:
            return

        def validate(value: dict[str, Any]) -> None:
            outputs = value.get("vrr")
            windows = value.get("windows")
            if (not isinstance(outputs, list) or len(outputs) != 1 or
                    not isinstance(outputs[0], dict) or
                    outputs[0].get("name") != self.config["connector"]):
                raise HarnessError("expected exactly the selected VRR output")
            if windows != []:
                raise HarnessError(
                    "client windows remain in the committed snapshot")
            candidate = outputs[0].get("candidate_window")
            if (isinstance(candidate, bool) or
                    not isinstance(candidate, int) or candidate != 0):
                raise HarnessError(
                    "a VRR candidate remains in the committed snapshot")

        self._bounded_json_query(
            "policy-cleanup", "coordinated client cleanup",
            [str(FIXED_BINARIES["gwinfo"]), "--socket",
             str(RUNTIME_ROOT / "control.sock"), "vrr", "--json"],
            self.artifacts / ".policy-cleanup.query.tmp",
            CLEANUP_QUERY_ATTEMPTS, validate,
        )

    def request_bounded_repaint(self) -> None:
        if not self.validate_runtime:
            return
        trigger = RUNTIME_ROOT / "repaint.request"
        report = self.artifacts / "vrr-part-1.jsonl"
        report_start = self.acquire_stable_report_boundary(report)
        try:
            descriptor = os.open(
                trigger,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC |
                os.O_NOFOLLOW,
                0o600,
            )
        except FileExistsError as error:
            raise HarnessError(
                "bounded repaint trigger already exists") from error
        except OSError as error:
            raise HarnessError(
                f"could not create bounded repaint trigger: {error}") from error
        else:
            os.close(descriptor)
        try:
            status = trigger.lstat()
        except OSError as error:
            raise HarnessError(
                f"could not inspect bounded repaint trigger: {error}") from error
        if (not stat.S_ISREG(status.st_mode) or
                status.st_mode & 0o077):
            raise HarnessError("bounded repaint trigger is not private")
        self.wait_absent(trigger)
        self.wait_for_sealed_presentation(report_start)

    def acquire_stable_report_boundary(self, report: Path) -> int:
        previous: bytes | None = None
        for _ in range(200):
            contents = _read_regular(report, MAX_JSON_BYTES)
            if previous is not None and not contents.startswith(previous):
                raise HarnessError(
                    "bounded repaint VRR report changed before its "
                    "append boundary")
            if (previous is not None and contents == previous and
                    (not contents or contents[-1:] == b"\n")):
                return len(contents)
            previous = contents
            time.sleep(.05)
        raise HarnessError(
            "bounded repaint VRR report did not reach a stable newline "
            "boundary")

    def wait_for_sealed_presentation(self, report_start: int) -> None:
        if not self.validate_runtime:
            return
        report = self.artifacts / "vrr-part-1.jsonl"
        drm_report = self.artifacts / "milestone14-drm-report.jsonl"
        for _ in range(200):
            contents = _read_regular(report, MAX_JSON_BYTES)
            if (report_start > len(contents) or
                    (report_start > 0 and
                     contents[report_start - 1:report_start] != b"\n")):
                raise HarnessError(
                    "bounded repaint VRR report boundary is invalid")
            tail = contents[report_start:]
            if tail and tail[-1:] == b"\n":
                try:
                    records = [
                        json.loads(line) for line in
                        tail.decode("utf-8").splitlines() if line.strip()
                    ]
                    drm_records = [
                        json.loads(line) for line in
                        _read_regular(drm_report, MAX_JSON_BYTES)
                        .decode("utf-8").splitlines() if line.strip()
                    ]
                    accepted = sealed_vrr_records(
                        records, require_seals=True,
                        drm_streams=drm_evidence_streams(drm_records),
                    )
                    if any(record.get("record") == "vrr-decision"
                           for record in accepted):
                        return
                except (HarnessError, json.JSONDecodeError, UnicodeError):
                    pass
            time.sleep(.05)
        raise HarnessError(
            "bounded repaint did not produce sealed presentation evidence")

    def wait_for_fresh_compositor_presentation(self) -> None:
        if not self.validate_runtime:
            return
        report = self.artifacts / "vrr-part-1.jsonl"
        drm_report = self.artifacts / "milestone14-drm-report.jsonl"
        for _ in range(200):
            try:
                records = [
                    json.loads(line) for line in
                    _read_regular(report, MAX_JSON_BYTES)
                    .decode("utf-8").splitlines() if line.strip()
                ]
                drm_records = [
                    json.loads(line) for line in
                    _read_regular(drm_report, MAX_JSON_BYTES)
                    .decode("utf-8").splitlines() if line.strip()
                ]
                accepted = sealed_vrr_records(
                    records, require_seals=True,
                    drm_streams=drm_evidence_streams(drm_records),
                )
                if any(record.get("record") == "vrr-decision"
                       for record in accepted):
                    return
            except (HarnessError, json.JSONDecodeError, UnicodeError):
                pass
            time.sleep(.05)
        raise HarnessError(
            "replacement compositor did not produce sealed replay evidence")

    def verify_active_vt_reevaluation(self) -> None:
        self.request_bounded_repaint()
        self.snapshot("milestone14-vt-active.json",
                      "always-eligible", True)

    def verify_compositor_restart(self) -> None:
        self.wait_for_fresh_compositor_presentation()
        self.snapshot("milestone14-restart.log",
                      "always-eligible", True)

    def snapshot(self, name: str, policy: str, effective: bool,
                 preference: str | None = None,
                 output_reasons: tuple[str, ...] | None = None,
                 window_reasons: tuple[str, ...] | None = None) -> dict[str, Any]:
        self.snapshot_expectations.append(
            {"name": name, "policy": policy, "effective": effective,
             "preference": preference, "output_reasons": output_reasons,
             "window_reasons": window_reasons})
        if not self.validate_runtime:
            self.command([str(FIXED_BINARIES["gwinfo"]), "--socket",
                          str(RUNTIME_ROOT / "control.sock"), "vrr",
                          str(self.config["connector"]), "--json"], name)
            return {}
        path = self.artifacts / name
        temporary = self.artifacts / f".{name}.query.tmp"

        def validate(value: dict[str, Any]) -> None:
            outputs = value.get("vrr")
            if not isinstance(outputs, list) or len(outputs) != 1:
                raise HarnessError("expected exactly one VRR output")
            output = outputs[0]
            if (output.get("name") != self.config["connector"] or
                    output.get("policy") != policy or
                    output.get("effective_enabled") is not effective or
                    output.get("hardware_capable") is not True or
                    output.get("kms_controllable") is not True or
                    output.get("simulated") is not False):
                raise HarnessError("exact VRR output state has not converged")
            if (output_reasons is not None and
                    vrr_rejection_reasons(output.get("reasons")) !=
                    list(output_reasons)):
                raise HarnessError(
                    "exact VRR output reasons have not converged")
            if preference is None:
                return
            windows = value.get("windows")
            window = next(
                (item for item in windows
                 if isinstance(item, dict) and
                 item.get("preference", "").lower() == preference),
                None,
            ) if isinstance(windows, list) else None
            if window is None:
                raise HarnessError(
                    f"{preference} window state has not converged")
            if (window_reasons is not None and
                    window.get("reasons") != list(window_reasons)):
                raise HarnessError(
                    "exact VRR window reasons have not converged")

        return self._bounded_json_query(
            name, name,
            [str(FIXED_BINARIES["gwinfo"]), "--socket",
             str(RUNTIME_ROOT / "control.sock"), "vrr",
             str(self.config["connector"]), "--json"],
            temporary, QUERY_ATTEMPTS, validate, path,
        )

    def _step(self, number: int) -> None:
        if not self.record_full_steps:
            return
        if number != len(self.steps) + 1:
            raise AssertionError("hardware run step order is not contiguous")
        self.steps.append(RUN_STEPS[number - 1])

    def run_app_requested_scenarios(self) -> None:
        self.set_policy("app-requested")
        self.start_client("app-default", "windowed", "default")
        self.snapshot("milestone14-app-requested-default.json",
                      "app-requested", False, "default", ("no-candidate",),
                      ("window-did-not-request",))
        self.stop_client("app-default")
        self.wait_policy_cleanup()

        self.start_client("app-prefer", "app-requested")
        self.snapshot("milestone14-app-requested.log",
                      "app-requested", True, "prefer", (), ())
        self.stop_client("app-prefer")
        self.wait_policy_cleanup()

        self.start_client("app-preferences", "preference")
        self.snapshot("milestone14-app-requested-disable.json",
                      "app-requested", False, "disable", ("no-candidate",),
                      ("window-preference-disabled",
                       "window-did-not-request"))
        self.stop_client("app-preferences")
        self.wait_policy_cleanup()

    def preflight(self) -> None:
        if self.verify_paths:
            require_live_harness_scope()
            for path in FIXED_BINARIES.values():
                if not path.is_file() or path.is_symlink() or not os.access(path, os.X_OK):
                    raise HarnessError(f"fixed executable is unavailable or unsafe: {path}")
        if not self.detached_invocation:
            current = self.active_tty
            if current is None:
                try:
                    current = os.ttyname(sys.stdin.fileno())
                except OSError as error:
                    raise HarnessError(
                        "live run requires invocation from the configured "
                        "text VT or explicit --unattended") from error
            if current != self.config["tty"]:
                raise HarnessError("live run refused the wrong active VT")
        if self.command_result([
                str(FIXED_BINARIES["systemctl"]), "is-active",
                "display-manager.service"]).succeeded:
            raise HarnessError("live run refused an active graphical display manager")
        if self.verify_paths:
            self.prepare_unit_names()
        self.verify_live_console()

    def record_restoration_evidence(self) -> None:
        original = self.before_state
        restored = self.after_state
        checks: dict[str, bool] = {}
        errors = list(self.cleanup_errors)
        for field, label in (("active_vt", "active VT"),
                             ("kd_mode", "KD mode"),
                             ("getty_active", "getty state")):
            passed = (original is not None and restored is not None and
                      field in original and field in restored and
                      original[field] == restored[field])
            checks[field] = passed
            if not passed:
                expected = original.get(field) if original else None
                observed = restored.get(field) if restored else None
                errors.append(
                    f"{label} restoration mismatch: expected {expected}, "
                    f"observed {observed}")
        errors = list(dict.fromkeys(errors))
        self.cleanup_errors = errors
        self.restoration_evidence = {
            "schema": "glasswyrm.m14-hardware.v1",
            "original_console": original,
            "restored_console": restored,
            "checks": checks,
            "errors": errors,
            "readback_success": all(checks.values()),
            "passed": all(checks.values()) and not errors,
        }
        try:
            _write_json(self.artifacts / "milestone14-restore.json",
                        self.restoration_evidence)
        except OSError as error:
            self.cleanup_errors.append(
                f"restoration evidence write failed: {error}")

    def cleanup(self) -> None:
        self.cleanup_attempted = True
        operations: list[list[str]] = []
        managed_units = list(reversed(self.managed_units))
        if self.before_state and self.before_state.get("active_vt"):
            operations.append([str(FIXED_BINARIES["chvt"]),
                               str(self.before_state["active_vt"])])
        if self.getty_stopped:
            operations.append([str(FIXED_BINARIES["systemctl"]),
                               "start" if self.getty_was_active else "stop",
                               self.getty_unit])
        for unit in managed_units:
            try:
                self.stop_unit(unit)
            except Exception as error:  # restoration must continue after every failure
                self.cleanup_errors.append(f"cleanup exception: {error}")
        for argv in operations:
            try:
                if not self.command_result(argv).succeeded:
                    self.cleanup_errors.append(f"cleanup command failed: {Path(argv[0]).name} {' '.join(argv[1:3])}")
            except Exception as error:  # restoration must continue after every failure
                self.cleanup_errors.append(f"cleanup exception: {error}")
        if self.verify_paths:
            after = self.artifacts / "kms-after.json"
            argv = [str(FIXED_BINARIES["drm-probe"]), "--device", str(self.config["drm_device"]),
                    "--connector", str(self.config["connector"]), "--require-mode",
                    str(self.config["mode"]).split("@", 1)[0], "--expect-restored",
                    str(self.artifacts / "kms-before.json"), "--output", str(after)]
            try:
                if not self.command_result(argv).succeeded:
                    self.cleanup_errors.append("exact KMS restoration probe failed")
            except Exception as error:
                self.cleanup_errors.append(f"KMS restoration probe exception: {error}")
        try:
            self.after_state = self.state_reader()
        except Exception as error:
            self.cleanup_errors.append(f"console readback failed: {error}")
        for path in (RUNTIME_ROOT / "control.sock", RUNTIME_ROOT / "gwcomp.sock",
                     RUNTIME_ROOT / "gwm.sock", RUNTIME_ROOT / "mirror.capture",
                     RUNTIME_ROOT / "repaint.request"):
            try:
                path.unlink(missing_ok=True)
            except OSError as error:
                self.cleanup_errors.append(f"runtime socket cleanup failed: {error}")
        try:
            RUNTIME_ROOT.rmdir()
        except OSError as error:
            if self.validate_runtime:
                self.cleanup_errors.append(f"runtime directory cleanup failed: {error}")
        self.record_restoration_evidence()

    @staticmethod
    def _handle_termination(signum: int, _frame: object) -> None:
        raise HarnessError(f"live run interrupted by signal {signum}")

    def stage_start_stack(self) -> None:
        self._step(1)
        self.before_state = self.state_reader()
        self.getty_was_active = bool(self.before_state["getty_active"])
        if self.verify_paths:
            self.command([
                str(FIXED_BINARIES["drm-probe"]), "--device",
                str(self.config["drm_device"]), "--connector",
                str(self.config["connector"]), "--require-mode",
                str(self.config["mode"]).split("@", 1)[0],
                "--snapshot-state", "--output",
                str(self.artifacts / "kms-before.json"),
            ])
        self.verify_live_console()
        self.command([
            str(FIXED_BINARIES["systemctl"]), "stop", self.getty_unit,
        ])
        self.getty_stopped = True
        self._step(2)
        self.start_unit(
            LIVE_UNITS["gwm"], "gwm",
            ["--ipc-socket", str(RUNTIME_ROOT / "gwm.sock")])
        self.wait_path(RUNTIME_ROOT / "gwm.sock")
        self._step(3)
        self.start_stack_after_gwm()
        self._step(4)
        self.start_server()

    def stage_stack_cadence(self) -> None:
        self._step(5)
        self.begin_cadence("off-cadence")
        self.start_client("off-cadence", "cadence", "default", True)
        self._step(6)
        self._step(7)
        self.snapshot("milestone14-off.json", "off", False)
        self.finish_cadence("off-cadence", False)
        self.stop_client("off-cadence")
        self.wait_policy_cleanup()
        self._step(8)
        self.set_policy("fullscreen")
        self._step(9)
        self.begin_cadence("on-cadence")
        self.start_client("on-cadence", "cadence", "default", True)
        self._step(10)
        self.snapshot("milestone14-fullscreen.log", "fullscreen", True)
        self._step(11)
        self.finish_cadence("on-cadence", True)
        self.stop_client("on-cadence")
        self.wait_policy_cleanup()
        self._step(12)
        self.snapshot("milestone14-fullscreen-exit.json", "fullscreen", False)

    def stage_policy_matrix(self) -> None:
        self._step(13)
        self.start_client("borderless", "borderless", "default")
        self._step(14)
        self.snapshot("milestone14-borderless.log", "fullscreen", True)
        self.stop_client("borderless")
        self.wait_policy_cleanup()
        self._step(15)
        self.set_policy("focused")
        self.start_client("focus-a", "windowed", "default")
        self.snapshot("milestone14-focused.log", "focused", True)
        self.start_client("focus-b", "windowed", "default")
        self.snapshot("milestone14-focused-transfer.json", "focused", True)
        self.stop_client("focus-b")
        self.stop_client("focus-a")
        self.wait_policy_cleanup()
        self._step(16)
        self.run_app_requested_scenarios()
        self._step(17)
        self.set_policy("always-eligible")
        self.start_client(
            "always", "windowed", "default", repaint=True)
        self.snapshot("milestone14-always.log", "always-eligible", True)
        self._step(18)
        self.set_policy("off")
        self.snapshot("milestone14-policy-off.json", "off", False)
        self.set_policy("always-eligible")

    def stage_vt_cycle(self) -> None:
        self._step(19)
        self.command([
            str(FIXED_BINARIES["chvt"]), self.alternate_tty,
        ])
        self.snapshot("milestone14-vt-inactive.json", "always-eligible", False)
        active_tty = TTY_PATTERN.fullmatch(
            str(self.config["tty"])).group(1)  # type: ignore[union-attr]
        self.command([str(FIXED_BINARIES["chvt"]), active_tty])
        self._step(20)
        if self.validate_runtime:
            shutil.copyfile(
                self.artifacts / "milestone14-vt-inactive.json",
                self.artifacts / "milestone14-vt.log")
        self._step(21)
        self.verify_active_vt_reevaluation()

    def stage_restart_gwm(self) -> None:
        self._step(22)
        gwm_socket = RUNTIME_ROOT / "gwm.sock"
        old_inode = gwm_socket.stat().st_ino if self.validate_runtime else 0
        self.command([
            str(FIXED_BINARIES["systemctl"]), "restart", LIVE_UNITS["gwm"],
        ])
        self.wait_replaced(gwm_socket, old_inode)
        self.snapshot("milestone14-restart-gwm.json", "always-eligible", True)

    def stage_restart_gwcomp(self) -> None:
        self._step(23)
        compositor_socket = RUNTIME_ROOT / "gwcomp.sock"
        self.stop_unit(LIVE_UNITS["gwcomp"])
        self.wait_absent(compositor_socket)
        if self.validate_runtime:
            os.replace(
                self.artifacts / "vrr-part-1.jsonl",
                self.artifacts / "vrr-part-0.jsonl")
            os.replace(
                self.artifacts / "milestone14-drm-report.jsonl",
                self.artifacts / "drm-part-0.jsonl")
        self.start_stack_after_gwm()
        self._step(24)
        self.verify_compositor_restart()

    def stage_pixel_parity(self) -> None:
        self._step(25)
        self.capture_pixels()

    def stage_shutdown_and_restore(self) -> None:
        self._step(26)
        self.stop_client("always")
        self._step(27)
        self.cleanup()
        self._step(28)
        self._step(29)

    def prepare_always_eligible_stage(self) -> None:
        self.set_policy("always-eligible")
        self.start_client(
            "always", "windowed", "default", repaint=True)
        self.snapshot("milestone14-always.log", "always-eligible", True)

    def run_stage(self, stage: str) -> None:
        if stage == "full-acceptance":
            self.run()
            return
        if stage not in DIAGNOSTIC_STACK_STAGES:
            raise HarnessError(f"unsupported live diagnostic stage: {stage}")
        self.record_full_steps = False
        self.preflight()
        previous_handlers: dict[int, Any] = {}
        try:
            if self.verify_paths:
                for signum, handler in (
                        (signal.SIGHUP, signal.SIG_IGN),
                        (signal.SIGINT, self._handle_termination),
                        (signal.SIGTERM, self._handle_termination)):
                    previous_handlers[signum] = signal.getsignal(signum)
                    signal.signal(signum, handler)
            self.stage_start_stack()
            if stage == "stack-cadence":
                self.stage_stack_cadence()
            elif stage == "policy-matrix":
                self.set_policy("fullscreen")
                self.stage_policy_matrix()
            else:
                self.prepare_always_eligible_stage()
                getattr(self, "stage_" + stage.replace("-", "_"))()
        finally:
            if not self.cleanup_attempted:
                self.cleanup()
            for signum, handler in previous_handlers.items():
                signal.signal(signum, handler)
        if (self.restoration_evidence is None or
                self.restoration_evidence.get("passed") is not True):
            raise HarnessError(
                f"{stage} completed without exact restoration evidence")

    def run(self) -> None:
        self.preflight()
        previous_handlers: dict[int, Any] = {}
        try:
            if self.verify_paths:
                for signum, handler in (
                        (signal.SIGHUP, signal.SIG_IGN),
                        (signal.SIGINT, self._handle_termination),
                        (signal.SIGTERM, self._handle_termination)):
                    previous_handlers[signum] = signal.getsignal(signum)
                    signal.signal(signum, handler)
            for stage in FULL_ACCEPTANCE_STAGES:
                method = getattr(self, "stage_" + stage.replace("-", "_"))
                method()
        finally:
            if not self.cleanup_attempted:
                self.cleanup()
            for signum, handler in previous_handlers.items():
                signal.signal(signum, handler)

    def start_stack_after_gwm(self) -> None:
        drm = str(self.config["drm_device"]); tty = str(self.config["tty"])
        mirror_trigger = RUNTIME_ROOT / "mirror.capture"
        self.start_unit(LIVE_UNITS["gwcomp"], "gwcomp",
                        ["--backend", "drm", "--ipc-socket", str(RUNTIME_ROOT / "gwcomp.sock"), "--drm-device", drm, "--tty", tty, "--connector", str(self.config["connector"]), "--mode", str(self.config["mode"]), "--drm-api", "atomic", "--renderer", "software", "--mirror-dump-dir", str(self.artifacts / "frames"), "--mirror-dump-trigger", str(mirror_trigger), "--drm-report", str(self.artifacts / "milestone14-drm-report.jsonl"), "--vrr-report", str(self.artifacts / "vrr-part-1.jsonl")],
                        ["PrivateDevices=no", "DevicePolicy=closed", f"DeviceAllow={drm} rw", f"DeviceAllow={tty} rw", "StandardInput=tty-force", f"TTYPath={tty}", "TTYReset=yes", "TTYVHangup=yes", "TTYVTDisallocate=no", "KillMode=mixed", "SuccessExitStatus=143"])
        self.wait_path(RUNTIME_ROOT / "gwcomp.sock")

    def start_server(self) -> None:
        keyboard = str(self.config["keyboard_device"]); pointer = str(self.config["pointer_device"])
        self.start_unit(LIVE_UNITS["server"], "server",
                        ["--display", "14", "--wm-socket", str(RUNTIME_ROOT / "gwm.sock"), "--compositor-socket", str(RUNTIME_ROOT / "gwcomp.sock"), "--software-content", "--output-model", "--control-socket", str(RUNTIME_ROOT / "control.sock"), "--game-compat", "--vrr-protocol", "--libinput-device", keyboard, "--libinput-device", pointer],
                        ["PrivateDevices=no", "DevicePolicy=closed", f"DeviceAllow={keyboard} r", f"DeviceAllow={pointer} r", "KillMode=mixed", "SuccessExitStatus=143"])
        self.wait_path(RUNTIME_ROOT / "control.sock"); self.wait_path(Path("/tmp/.X11-unix/X14"))

    def capture_pixels(self) -> None:
        if not self.validate_runtime:
            return
        trigger = RUNTIME_ROOT / "mirror.capture"

        def capture(policy: str, destination: str) -> None:
            frames = sorted((self.artifacts / "frames").glob("*.ppm"))
            count = len(frames)
            self.set_policy(policy)
            state_name = ("milestone14-capture-off-state.json" if policy == "off"
                          else "milestone14-capture-enabled-state.json")
            self.snapshot(state_name, policy,
                          policy == "always-eligible")
            trigger.touch(mode=0o600, exist_ok=False)
            self.request_bounded_repaint()
            for _ in range(200):
                frames = sorted((self.artifacts / "frames").glob("*.ppm"))
                if len(frames) > count:
                    shutil.copyfile(frames[-1], self.artifacts / destination)
                    return
                time.sleep(.05)
            raise HarnessError("one-shot mirror capture did not produce pixel evidence")

        capture("off", "milestone14-canonical.ppm")
        capture("always-eligible", "milestone14-screen.ppm")
        if (self.artifacts / "milestone14-canonical.ppm").read_bytes() != (self.artifacts / "milestone14-screen.ppm").read_bytes():
            raise HarnessError("VRR-only transition changed canonical pixels")
