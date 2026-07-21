"""Fail-closed, shell-free physical session orchestration."""

from __future__ import annotations

import fcntl
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import time
from typing import Any

from common import HarnessError, RUN_STEPS, TTY_PATTERN, _read_json

BUILD_ROOT = Path("/var/tmp/glasswyrm-build-m14")
RUNTIME_ROOT = Path("/run/glasswyrm-m14-hardware")
FIXED_BINARIES = {
    "gwm": BUILD_ROOT / "src/gwm",
    "gwcomp": BUILD_ROOT / "src/gwcomp",
    "server": BUILD_ROOT / "src/glasswyrmd",
    "gwout": BUILD_ROOT / "tools/gwout",
    "gwinfo": BUILD_ROOT / "tools/gwinfo",
    "client": BUILD_ROOT / "tests/manifest/m14/m14_vrr_client",
    "drm-probe": BUILD_ROOT / "tools/gw_drm_probe",
    "systemctl": Path("/usr/bin/systemctl"),
    "systemd-run": Path("/usr/bin/systemd-run"),
    "chvt": Path("/usr/bin/chvt"),
}

LIVE_UNITS = {
    "gwm": "gwm-m14-hardware.service",
    "gwcomp": "gwcomp-m14-hardware.service",
    "server": "glasswyrmd-m14-hardware.service",
}


class FixedLiveRunner:
    """Shell-free, fixed-argv physical runner with unconditional cleanup."""

    def __init__(self, config: dict[str, object], artifacts: Path,
                 execute: Any = None, active_tty: str | None = None,
                 verify_paths: bool = True, ready: Any = None,
                 state_reader: Any = None, validate_runtime: bool = True) -> None:
        self.config = config
        self.artifacts = artifacts
        self.execute = execute or self._execute
        self.active_tty = active_tty
        self.verify_paths = verify_paths
        self.ready = ready or self._ready
        self.state_reader = state_reader or self._console_state
        self.validate_runtime = validate_runtime
        self.steps: list[str] = []
        self.cleanup_attempted = False
        self.cleanup_errors: list[str] = []
        self.getty_stopped = False
        self.getty_was_active = False
        tty_number = TTY_PATTERN.fullmatch(str(config["tty"])).group(1)  # type: ignore[union-attr]
        self.getty_unit = f"getty@tty{tty_number}.service"
        self.alternate_tty = "1" if tty_number != "1" else "2"
        self.client_units: list[str] = []
        self.before_state: dict[str, object] | None = None
        self.after_state: dict[str, object] | None = None
        self.snapshot_expectations: list[dict[str, object]] = []
        self.cleanup_wait_count = 0

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

    def start_unit(self, name: str, executable: str, arguments: list[str],
                   properties: list[str] | None = None) -> None:
        argv = [str(FIXED_BINARIES["systemd-run"]), f"--unit={name.removesuffix('.service')}",
                "--property=Type=simple", "--no-block"]
        for value in properties or []:
            argv.append(f"--property={value}")
        argv += ["--", str(FIXED_BINARIES[executable]), *arguments]
        self.command(argv)

    def set_policy(self, policy: str) -> None:
        self.command([str(FIXED_BINARIES["gwout"]), "--socket",
                      str(RUNTIME_ROOT / "control.sock"), "set",
                      str(self.config["connector"]), "--vrr", policy, "--json"])

    def start_client(self, tag: str, mode: str, preference: str | None = None,
                     cadence: bool = False) -> Path:
        unit = f"m14-hardware-client-{tag}.service"
        result = self.artifacts / f"client-{tag}.json"
        arguments = ["--display", ":14", "--mode", mode, "--result", str(result),
                     "--hold-ms", "30000"]
        if cadence:
            arguments += ["--frames", "180", "--target-refresh-hz",
                          str(self.config["target_refresh_hz"])]
        if preference is not None:
            arguments += ["--preference", preference]
        self.start_unit(unit, "client", arguments,
                        ["KillMode=mixed", "SuccessExitStatus=143"])
        self.client_units.append(unit)
        self.wait_path(result, "file")
        if self.validate_runtime:
            state = _read_json(result)
            if (state.get("schema") != "glasswyrm.m14-vrr-client.v2" or
                    state.get("mode") != mode or (preference is not None and
                    state.get("preference", "").lower() != preference)):
                raise HarnessError(f"client {tag} published unexpected state")
        return result

    def stop_client(self, tag: str) -> None:
        unit = f"m14-hardware-client-{tag}.service"
        self.command([str(FIXED_BINARIES["systemctl"]), "stop", unit])

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
                 preference: str | None = None) -> dict[str, Any]:
        self.snapshot_expectations.append(
            {"name": name, "policy": policy, "effective": effective,
             "preference": preference})
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
                if preference is not None:
                    windows = value.get("windows")
                    if not isinstance(windows, list) or not any(
                            item.get("preference", "").lower() == preference
                            for item in windows if isinstance(item, dict)):
                        raise HarnessError(f"{preference} window state has not converged")
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
                      "app-requested", False, "default")
        self.stop_client("app-default")
        self.wait_policy_cleanup()

        self.start_client("app-prefer", "app-requested")
        self.snapshot("milestone14-app-requested.log",
                      "app-requested", True, "prefer")
        self.stop_client("app-prefer")
        self.wait_policy_cleanup()

        self.start_client("app-preferences", "preference")
        self.snapshot("milestone14-app-requested-disable.json",
                      "app-requested", False, "disable")
        self.stop_client("app-preferences")
        self.wait_policy_cleanup()

    def preflight(self) -> None:
        if self.verify_paths:
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

    def cleanup(self) -> None:
        self.cleanup_attempted = True
        operations: list[list[str]] = []
        for unit in reversed(self.client_units):
            operations.append([str(FIXED_BINARIES["systemctl"]), "stop", unit])
        operations += [
            [str(FIXED_BINARIES["systemctl"]), "stop", LIVE_UNITS["server"]],
            [str(FIXED_BINARIES["systemctl"]), "stop", LIVE_UNITS["gwcomp"]],
            [str(FIXED_BINARIES["systemctl"]), "stop", LIVE_UNITS["gwm"]],
        ]
        if self.before_state and self.before_state.get("active_vt"):
            operations.append([str(FIXED_BINARIES["chvt"]),
                               str(self.before_state["active_vt"])])
        if self.getty_stopped:
            operations.append([str(FIXED_BINARIES["systemctl"]),
                               "start" if self.getty_was_active else "stop",
                               self.getty_unit])
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
                     RUNTIME_ROOT / "gwm.sock"):
            try:
                path.unlink(missing_ok=True)
            except OSError as error:
                self.cleanup_errors.append(f"runtime socket cleanup failed: {error}")
        try:
            RUNTIME_ROOT.rmdir()
        except OSError as error:
            if self.validate_runtime:
                self.cleanup_errors.append(f"runtime directory cleanup failed: {error}")

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
            self.command([str(FIXED_BINARIES["systemctl"]), "stop", self.getty_unit])
            self.getty_stopped = True
            self._step(2); self.start_unit(LIVE_UNITS["gwm"], "gwm", ["--ipc-socket", str(RUNTIME_ROOT / "gwm.sock")]); self.wait_path(RUNTIME_ROOT / "gwm.sock")
            self._step(3)
            # Start compositor and server through the same fixed builder used by start_stack.
            self.start_stack_after_gwm()
            self._step(4); self.start_server()
            self._step(5); self.start_client("off-cadence", "cadence", "default", True)
            self._step(6)
            self._step(7); self.snapshot("milestone14-off.json", "off", False); self.stop_client("off-cadence"); self.wait_policy_cleanup()
            self._step(8); self.set_policy("fullscreen")
            self._step(9); self.start_client("on-cadence", "cadence", "default", True)
            self._step(10); self.snapshot("milestone14-fullscreen.log", "fullscreen", True)
            self._step(11); self.stop_client("on-cadence"); self.wait_policy_cleanup()
            self._step(12); self.snapshot("milestone14-fullscreen-exit.json", "fullscreen", False)
            self._step(13); self.start_client("borderless", "borderless", "default")
            self._step(14); self.snapshot("milestone14-borderless.log", "fullscreen", True); self.stop_client("borderless"); self.wait_policy_cleanup()
            self._step(15); self.set_policy("focused"); self.start_client("focus-a", "windowed", "default"); self.snapshot("milestone14-focused.log", "focused", True); self.start_client("focus-b", "windowed", "default"); self.snapshot("milestone14-focused-transfer.json", "focused", True); self.stop_client("focus-b"); self.stop_client("focus-a"); self.wait_policy_cleanup()
            self._step(16); self.run_app_requested_scenarios()
            self._step(17); self.set_policy("always-eligible"); self.start_client("always", "windowed", "default"); self.snapshot("milestone14-always.log", "always-eligible", True)
            self._step(18); self.set_policy("off"); self.snapshot("milestone14-policy-off.json", "off", False); self.set_policy("always-eligible")
            self._step(19); self.command([str(FIXED_BINARIES["chvt"]), self.alternate_tty]); self.snapshot("milestone14-vt-inactive.json", "always-eligible", False); self.command([str(FIXED_BINARIES["chvt"]), TTY_PATTERN.fullmatch(str(self.config["tty"])).group(1)])  # type: ignore[union-attr]
            self._step(20); shutil.copyfile(self.artifacts / "milestone14-vt-inactive.json", self.artifacts / "milestone14-vt.log") if self.validate_runtime else None
            self._step(21); self.snapshot("milestone14-vt-active.json", "always-eligible", True)
            self._step(22); gwm_socket = RUNTIME_ROOT / "gwm.sock"; old_inode = gwm_socket.stat().st_ino if self.validate_runtime else 0; self.command([str(FIXED_BINARIES["systemctl"]), "restart", LIVE_UNITS["gwm"]]); self.wait_replaced(gwm_socket, old_inode); self.snapshot("milestone14-restart-gwm.json", "always-eligible", True)
            self._step(23); compositor_socket = RUNTIME_ROOT / "gwcomp.sock"; self.command([str(FIXED_BINARIES["systemctl"]), "stop", LIVE_UNITS["gwcomp"]]); self.wait_absent(compositor_socket)
            if self.validate_runtime:
                os.replace(self.artifacts / "vrr-part-1.jsonl", self.artifacts / "vrr-part-0.jsonl")
                os.replace(self.artifacts / "milestone14-drm-report.jsonl", self.artifacts / "drm-part-0.jsonl")
            self.command([str(FIXED_BINARIES["systemctl"]), "start", LIVE_UNITS["gwcomp"]]); self.wait_path(compositor_socket)
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
        self.start_unit(LIVE_UNITS["gwcomp"], "gwcomp",
                        ["--backend", "drm", "--ipc-socket", str(RUNTIME_ROOT / "gwcomp.sock"), "--drm-device", drm, "--tty", tty, "--connector", str(self.config["connector"]), "--mode", str(self.config["mode"]), "--drm-api", "atomic", "--renderer", "software", "--mirror-dump-dir", str(self.artifacts / "frames"), "--drm-report", str(self.artifacts / "milestone14-drm-report.jsonl"), "--vrr-report", str(self.artifacts / "vrr-part-1.jsonl")],
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
        frames = sorted((self.artifacts / "frames").glob("*.ppm"))
        if not frames:
            raise HarnessError("no compositor mirror frame is available")
        shutil.copyfile(frames[-1], self.artifacts / "milestone14-canonical.ppm")
        count = len(frames); self.set_policy("off"); self.set_policy("always-eligible")
        for _ in range(200):
            frames = sorted((self.artifacts / "frames").glob("*.ppm"))
            if len(frames) > count:
                break
            time.sleep(.05)
        if len(frames) <= count:
            raise HarnessError("policy-only commit did not produce screen evidence")
        shutil.copyfile(frames[-1], self.artifacts / "milestone14-screen.ppm")
        if (self.artifacts / "milestone14-canonical.ppm").read_bytes() != (self.artifacts / "milestone14-screen.ppm").read_bytes():
            raise HarnessError("VRR-only transition changed canonical pixels")
