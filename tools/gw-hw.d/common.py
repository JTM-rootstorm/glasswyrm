"""Shared constants and safe file helpers for the fixed M14 harness."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import stat
from typing import Any

SCHEMA_KEYS = {
    "drm_device", "connector", "mode", "tty", "alternate_tty",
    "expected_min_refresh_hz",
    "expected_max_refresh_hz", "target_refresh_hz", "monitor_model",
    "edid_sha256", "debugfs_connector_path", "keyboard_device",
    "pointer_device", "required_base_commit", "tested_commit",
}
STRING_KEYS = {
    "drm_device", "connector", "mode", "tty", "alternate_tty", "monitor_model",
    "edid_sha256", "debugfs_connector_path", "keyboard_device",
    "pointer_device", "required_base_commit", "tested_commit",
}
INTEGER_KEYS = SCHEMA_KEYS - STRING_KEYS
MODE_PATTERN = re.compile(r"([1-9][0-9]*)x([1-9][0-9]*)@([1-9][0-9]*)")
CONNECTOR_PATTERN = re.compile(r"[A-Za-z0-9]+-[1-9][0-9]*")
DRM_PATTERN = re.compile(r"/dev/dri/card([0-9]+)")
TTY_PATTERN = re.compile(r"/dev/tty([1-9][0-9]*)")
INPUT_PATTERN = re.compile(r"/dev/input/event([0-9]+)")
EDID_PATTERN = re.compile(r"[0-9a-f]{64}")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
M14_REQUIRED_BASE_COMMIT = "6864ea631d61636289a21c7d2d6655a17be0c004"
MAX_CONFIG_BYTES = 16 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_ARTIFACT_BYTES = 128 * 1024 * 1024
MAX_INTERVALS = 512
MIN_ENABLED_INTERVALS = 120
ENABLED_PASS_PERCENT = 75.0
DISABLED_PASS_PERCENT = 25.0
ARTIFACT_SCHEMA = "glasswyrm.m14-hardware.v1"

DOCTOR_FACT_KEYS = {
    "schema", "root", "drm_device", "drm_primary_node", "tty",
    "tty_character_device", "tty_kd_text", "active_tty", "spare_tty",
    "alternate_tty", "alternate_tty_character_device", "alternate_tty_kd_text",
    "alternate_tty_safe", "connector", "connected", "active",
    "connected_connector_count", "active_connector_count",
    "single_connected_active_connector",
    "edid_sha256", "vrr_capable", "atomic_kms", "vrr_enabled_property",
    "mode", "range_source", "minimum_refresh_hz", "maximum_refresh_hz",
    "no_competing_drm_master", "session_permissions", "kernel", "libdrm",
    "driver", "firmware", "keyboard_device", "pointer_device",
    "keyboard_character_device", "pointer_character_device",
    "selected_mode_available",
}
REQUIRED_ARTIFACTS = (
    "milestone14-hardware-doctor.json",
    "milestone14-hardware-config.json",
    "milestone14-drm-capability.json",
    "milestone14-vrr-report.jsonl",
    "milestone14-vrr-off-summary.json",
    "milestone14-vrr-on-summary.json",
    "milestone14-fullscreen.log",
    "milestone14-borderless.log",
    "milestone14-focused.log",
    "client-app-default.json",
    "client-app-prefer.json",
    "client-app-preferences.json",
    "milestone14-app-requested-default.json",
    "milestone14-app-requested.log",
    "milestone14-app-requested-disable.json",
    "milestone14-always.log",
    "milestone14-vt.log",
    "milestone14-restart.log",
    "milestone14-restore.json",
    "milestone14-hardware-summary.json",
    "milestone14-canonical.ppm",
    "milestone14-screen.ppm",
)
ARCHIVE_STATE_ARTIFACTS = (
    "milestone14-state-before.json", "milestone14-state-after.json",
)
FIXTURE_COPY_ARTIFACTS = (
    "milestone14-vrr-report.jsonl", "milestone14-fullscreen.log",
    "milestone14-borderless.log", "milestone14-focused.log",
    "client-app-default.json", "client-app-prefer.json",
    "client-app-preferences.json",
    "milestone14-app-requested-default.json",
    "milestone14-app-requested.log",
    "milestone14-app-requested-disable.json", "milestone14-always.log",
    "milestone14-vt.log", "milestone14-restart.log",
    "milestone14-canonical.ppm", "milestone14-screen.ppm",
)


class HarnessError(ValueError):
    pass


class ConfigError(HarnessError):
    pass


def interval_tolerance(target_ns: float) -> float:
    return max(250_000.0, target_ns * 0.01)


def _safe_text(name: str, value: str) -> None:
    if not value or len(value) > 4096 or any(ord(char) < 32 for char in value):
        raise HarnessError(f"{name} must be nonempty printable text")


def _read_regular(path: Path, maximum: int) -> bytes:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as error:
        raise HarnessError(f"{path.name} is unavailable: {error}") from error
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode):
            raise HarnessError(f"{path.name} must be a regular non-symlink file")
        if status.st_size > maximum:
            raise HarnessError(f"{path.name} exceeds {maximum} bytes")
        chunks: list[bytes] = []
        remaining = maximum + 1
        while remaining:
            chunk = os.read(descriptor, min(remaining, 64 * 1024))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        contents = b"".join(chunks)
        if len(contents) > maximum:
            raise HarnessError(f"{path.name} exceeds {maximum} bytes")
        return contents
    finally:
        os.close(descriptor)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(_read_regular(path, MAX_JSON_BYTES).decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise HarnessError(f"invalid {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise HarnessError(f"{path.name} must contain one JSON object")
    return value


def _write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _prepare_private_empty_directory(path: Path, label: str) -> None:
    if path.exists():
        status = path.lstat()
        if (not stat.S_ISDIR(status.st_mode) or status.st_mode & 0o077 or
                any(path.iterdir())):
            raise HarnessError(f"{label} must be a private empty non-symlink directory")
    else:
        path.mkdir(mode=0o700, parents=True)


RUN_STEPS = (
    "capture original KMS, VRR, KD, VT, and getty state", "start GWM",
    "start DRM gwcomp with VRR report", "start real-input glasswyrmd with output model/control/GW_VRR",
    "launch m14_vrr_client", "run policy Off cadence", "verify property zero and negative timing",
    "policy Fullscreen", "enter fullscreen", "verify property one and positive timing",
    "leave fullscreen", "verify property zero", "enter borderless fullscreen", "verify property one",
    "policy Focused and windowed focus test", "policy AppRequested no-request/Prefer/Disable",
    "policy AlwaysEligible", "return policy Off", "run VT release/acquire while eligible",
    "verify VRR disabled during inactive state", "verify reevaluation after Active", "restart GWM",
    "restart compositor", "verify candidate/effective state replay",
    "capture a canonical frame/screenshot to prove pixels remain correct", "stop client/session cleanly",
    "restore original VRR/KMS/KD/VT/getty state", "verify exact readback", "validate the archive",
)
