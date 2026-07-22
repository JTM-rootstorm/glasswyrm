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
import time
from typing import Any

from common import (
    BUILD_ROOT, HarnessError, MAX_JSON_BYTES, MIN_ENABLED_INTERVALS, RUN_STEPS,
    TTY_PATTERN, _read_json, _read_regular, _write_json,
    vrr_rejection_reasons,
)
from provenance import PROVENANCE_BINARIES

RUNTIME_ROOT = Path("/run/glasswyrm-m14-hardware")
LIVE_HARNESS_SCOPE = "glasswyrm-m14-harness.scope"
FIXED_BINARIES = {
    **PROVENANCE_BINARIES,
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
                 preflight_reader: Any = None) -> None:
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

    @staticmethod
    def _execute(argv: list[str], output: Path | None = None) -> int:
        allowed = {str(path) for path in FIXED_BINARIES.values()}
        if not argv or argv[0] not in allowed:
            raise HarnessError("live runner rejected a non-fixed executable")
        stream = output.open("ab") if output else subprocess.DEVNULL
        try:
            result = subprocess.run(argv, check=False, stdin=subprocess.DEVNULL,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=120)
            return result.returncode
        finally:
            if output:
                stream.close()

    def command(self, argv: list[str], output: str | None = None) -> None:
        if self.execute(argv, self.artifacts / output if output else None) != 0:
            raise HarnessError(f"fixed command failed: {Path(argv[0]).name}")

    @staticmethod
    def _ready(path: Path, kind: str) -> bool:
        if kind == "socket":
            try:
                return stat.S_ISSOCK(path.stat().st_mode)
            except OSError:
                return False
        return path.is_file() and path.stat().st_size > 0

    def wait_path(self, path: Path, kind: str = "socket") -> None:
        for _ in range(200):
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
        getty_active = self.execute(
            [str(FIXED_BINARIES["systemctl"]), "is-active", self.getty_unit], None) == 0
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
            if self.execute(argv, path) != 0:
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
        stop_result = self.execute(argv, None)
        if self.verify_paths:
            reset_attempted = False
            for _ in range(20):
                load_state, active_state = self.unit_state(name)
                if load_state == "not-found" and active_state == "inactive":
                    break
                if (load_state == "loaded" and
                        active_state in {"inactive", "failed"} and
                        not reset_attempted):
                    if self.execute([str(FIXED_BINARIES["systemctl"]),
                                     "reset-failed", name], None) != 0:
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
                    f"(stop={stop_result}, {load_state}/{active_state})")
        elif stop_result != 0:
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
                          str(self.config["target_refresh_hz"])]
        if preference is not None:
            arguments += ["--preference", preference]
        if repaint:
            arguments += ["--repaint-trigger",
                          str(RUNTIME_ROOT / "repaint.request"),
                          "--repaint-count", "2"]
        self.start_unit(unit, "client", arguments,
                        ["KillMode=mixed", "SuccessExitStatus=143"])
        self.wait_path(result, "file")
        if self.validate_runtime:
            state = _read_json(result)
            if (state.get("schema") != "glasswyrm.m14-vrr-client.v2" or
                    state.get("mode") != mode or (preference is not None and
                    state.get("preference", "").lower() != preference)):
                raise HarnessError(f"client {tag} published unexpected state")
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
            for number, line in enumerate(
                    contents[start:end].decode("utf-8").splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    raise HarnessError(
                        f"invalid {tag} cadence JSONL line {number}: {error}") from error
                if (isinstance(record, dict) and
                        record.get("record") in {"vrr-timing", "timing"} and
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
        last_error = "state was not observed"
        path = self.artifacts / ".policy-cleanup.tmp"
        for _ in range(400):
            path.unlink(missing_ok=True)
            argv = [str(FIXED_BINARIES["gwinfo"]), "--socket",
                    str(RUNTIME_ROOT / "control.sock"), "vrr", "--json"]
            if self.execute(argv, path) != 0:
                last_error = "gwinfo query failed"
                time.sleep(.05)
                continue
            try:
                value = _read_json(path)
                outputs = value.get("vrr")
                windows = value.get("windows")
                if (not isinstance(outputs, list) or len(outputs) != 1 or
                        not isinstance(outputs[0], dict) or
                        outputs[0].get("name") != self.config["connector"]):
                    raise HarnessError("expected exactly the selected VRR output")
                if windows != []:
                    raise HarnessError("client windows remain in the committed snapshot")
                candidate = outputs[0].get("candidate_window")
                if (isinstance(candidate, bool) or not isinstance(candidate, int) or
                        candidate != 0):
                    raise HarnessError("a VRR candidate remains in the committed snapshot")
                path.unlink(missing_ok=True)
                return
            except HarnessError as error:
                last_error = str(error)
                time.sleep(.05)
        raise HarnessError(
            f"timed out waiting for coordinated client cleanup: {last_error}")

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
        last_error = "state was not observed"
        for attempt in range(200):
            temporary = self.artifacts / f".{name}.attempt-{attempt}.tmp"
            temporary.unlink(missing_ok=True)
            self.command([str(FIXED_BINARIES["gwinfo"]), "--socket",
                          str(RUNTIME_ROOT / "control.sock"), "vrr",
                          str(self.config["connector"]), "--json"], temporary.name)
            try:
                value = _read_json(temporary)
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
                    raise HarnessError("exact VRR output reasons have not converged")
                if preference is not None:
                    windows = value.get("windows")
                    window = next((item for item in windows
                                   if isinstance(item, dict) and
                                   item.get("preference", "").lower() == preference),
                                  None) if isinstance(windows, list) else None
                    if window is None:
                        raise HarnessError(f"{preference} window state has not converged")
                    if (window_reasons is not None and
                            window.get("reasons") != list(window_reasons)):
                        raise HarnessError(
                            "exact VRR window reasons have not converged")
                os.replace(temporary, path)
                return value
            except HarnessError as error:
                last_error = str(error)
                temporary.unlink(missing_ok=True)
                time.sleep(.05)
        raise HarnessError(f"{name} timed out waiting for exact VRR state: {last_error}")

    def _step(self, number: int) -> None:
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
                      ("window-preference-disabled",))
        self.stop_client("app-preferences")
        self.wait_policy_cleanup()

    def preflight(self) -> None:
        if self.verify_paths:
            require_live_harness_scope()
            for path in FIXED_BINARIES.values():
                if not path.is_file() or path.is_symlink() or not os.access(path, os.X_OK):
                    raise HarnessError(f"fixed executable is unavailable or unsafe: {path}")
        current = self.active_tty
        if current is None:
            try:
                current = os.ttyname(sys.stdin.fileno())
            except OSError as error:
                raise HarnessError("live run requires invocation from the configured text VT") from error
        if current != self.config["tty"]:
            raise HarnessError("live run refused the wrong active VT")
        if self.execute([str(FIXED_BINARIES["systemctl"]), "is-active", "display-manager.service"], None) == 0:
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
                if self.execute(argv, None) != 0:
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
                if self.execute(argv, None) != 0:
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

    def run(self) -> None:
        self.preflight()
        try:
            self._step(1)
            self.before_state = self.state_reader()
            self.getty_was_active = bool(self.before_state["getty_active"])
            if self.verify_paths:
                self.command([str(FIXED_BINARIES["drm-probe"]), "--device", str(self.config["drm_device"]),
                              "--connector", str(self.config["connector"]), "--require-mode",
                              str(self.config["mode"]).split("@", 1)[0], "--snapshot-state", "--output",
                              str(self.artifacts / "kms-before.json")])
            self.verify_live_console()
            if self.verify_paths:
                signal.signal(signal.SIGHUP, signal.SIG_IGN)
            self.command([str(FIXED_BINARIES["systemctl"]), "stop", self.getty_unit])
            self.getty_stopped = True
            self._step(2); self.start_unit(LIVE_UNITS["gwm"], "gwm", ["--ipc-socket", str(RUNTIME_ROOT / "gwm.sock")]); self.wait_path(RUNTIME_ROOT / "gwm.sock")
            self._step(3)
            # Start compositor and server through the same fixed builder used by start_stack.
            self.start_stack_after_gwm()
            self._step(4); self.start_server()
            self._step(5); self.begin_cadence("off-cadence"); self.start_client("off-cadence", "cadence", "default", True)
            self._step(6)
            self._step(7); self.snapshot("milestone14-off.json", "off", False); self.finish_cadence("off-cadence", False); self.stop_client("off-cadence"); self.wait_policy_cleanup()
            self._step(8); self.set_policy("fullscreen")
            self._step(9); self.begin_cadence("on-cadence"); self.start_client("on-cadence", "cadence", "default", True)
            self._step(10); self.snapshot("milestone14-fullscreen.log", "fullscreen", True)
            self._step(11); self.finish_cadence("on-cadence", True); self.stop_client("on-cadence"); self.wait_policy_cleanup()
            self._step(12); self.snapshot("milestone14-fullscreen-exit.json", "fullscreen", False)
            self._step(13); self.start_client("borderless", "borderless", "default")
            self._step(14); self.snapshot("milestone14-borderless.log", "fullscreen", True); self.stop_client("borderless"); self.wait_policy_cleanup()
            self._step(15); self.set_policy("focused"); self.start_client("focus-a", "windowed", "default"); self.snapshot("milestone14-focused.log", "focused", True); self.start_client("focus-b", "windowed", "default"); self.snapshot("milestone14-focused-transfer.json", "focused", True); self.stop_client("focus-b"); self.stop_client("focus-a"); self.wait_policy_cleanup()
            self._step(16); self.run_app_requested_scenarios()
            self._step(17); self.set_policy("always-eligible"); self.start_client("always", "windowed", "default", repaint=True); self.snapshot("milestone14-always.log", "always-eligible", True)
            self._step(18); self.set_policy("off"); self.snapshot("milestone14-policy-off.json", "off", False); self.set_policy("always-eligible")
            self._step(19); self.command([str(FIXED_BINARIES["chvt"]), self.alternate_tty]); self.snapshot("milestone14-vt-inactive.json", "always-eligible", False); self.command([str(FIXED_BINARIES["chvt"]), TTY_PATTERN.fullmatch(str(self.config["tty"])).group(1)])  # type: ignore[union-attr]
            self._step(20); shutil.copyfile(self.artifacts / "milestone14-vt-inactive.json", self.artifacts / "milestone14-vt.log") if self.validate_runtime else None
            self._step(21); self.snapshot("milestone14-vt-active.json", "always-eligible", True)
            self._step(22); gwm_socket = RUNTIME_ROOT / "gwm.sock"; old_inode = gwm_socket.stat().st_ino if self.validate_runtime else 0; self.command([str(FIXED_BINARIES["systemctl"]), "restart", LIVE_UNITS["gwm"]]); self.wait_replaced(gwm_socket, old_inode); self.snapshot("milestone14-restart-gwm.json", "always-eligible", True)
            self._step(23); compositor_socket = RUNTIME_ROOT / "gwcomp.sock"; self.stop_unit(LIVE_UNITS["gwcomp"]); self.wait_absent(compositor_socket)
            if self.validate_runtime:
                os.replace(self.artifacts / "vrr-part-1.jsonl", self.artifacts / "vrr-part-0.jsonl")
                os.replace(self.artifacts / "milestone14-drm-report.jsonl", self.artifacts / "drm-part-0.jsonl")
            self.start_stack_after_gwm()
            self._step(24); self.snapshot("milestone14-restart.log", "always-eligible", True)
            self._step(25); self.capture_pixels()
            self._step(26); self.stop_client("always")
            self._step(27); self.cleanup()
            self._step(28)
            self._step(29)
        finally:
            if not self.cleanup_attempted:
                self.cleanup()

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
            repaint_trigger = RUNTIME_ROOT / "repaint.request"
            trigger.touch(mode=0o600, exist_ok=False)
            repaint_trigger.touch(mode=0o600, exist_ok=False)
            for _ in range(200):
                frames = sorted((self.artifacts / "frames").glob("*.ppm"))
                if not repaint_trigger.exists() and len(frames) > count:
                    shutil.copyfile(frames[-1], self.artifacts / destination)
                    return
                time.sleep(.05)
            raise HarnessError("one-shot mirror capture did not produce pixel evidence")

        capture("off", "milestone14-canonical.ppm")
        capture("always-eligible", "milestone14-screen.ppm")
        if (self.artifacts / "milestone14-canonical.ppm").read_bytes() != (self.artifacts / "milestone14-screen.ppm").read_bytes():
            raise HarnessError("VRR-only transition changed canonical pixels")
