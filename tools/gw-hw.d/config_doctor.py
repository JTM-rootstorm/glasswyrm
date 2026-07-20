"""Strict configuration parsing and read-only hardware doctor facts."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tomllib
from decimal import Decimal, InvalidOperation
from typing import Any

from common import (
    ARTIFACT_SCHEMA, CONNECTOR_PATTERN, ConfigError, DOCTOR_FACT_KEYS,
    DRM_PATTERN, EDID_PATTERN, HarnessError, INPUT_PATTERN, INTEGER_KEYS,
    MAX_CONFIG_BYTES, MODE_PATTERN, SCHEMA_KEYS, STRING_KEYS, TTY_PATTERN,
    _read_json, _read_regular, _safe_text, _write_json, interval_tolerance,
)

def parse_config(path: Path) -> dict[str, object]:
    try:
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_mode & 0o022:
            raise ConfigError("configuration must be a regular non-symlink file and not group/world writable")
        parsed = tomllib.loads(_read_regular(path, MAX_CONFIG_BYTES).decode("utf-8"))
    except (HarnessError, OSError) as error:
        raise ConfigError(str(error)) from error
    except (UnicodeError, tomllib.TOMLDecodeError) as error:
        raise ConfigError(f"invalid configuration: {error}") from error
    keys = set(parsed)
    if keys != SCHEMA_KEYS:
        missing = ", ".join(sorted(SCHEMA_KEYS - keys)) or "none"
        unknown = ", ".join(sorted(keys - SCHEMA_KEYS)) or "none"
        raise ConfigError(f"schema mismatch; missing: {missing}; unknown: {unknown}")
    for name in STRING_KEYS:
        if not isinstance(parsed[name], str):
            raise ConfigError(f"{name} must be a quoted string")
        _safe_text(name, parsed[name])
    for name in INTEGER_KEYS:
        if isinstance(parsed[name], bool) or not isinstance(parsed[name], int):
            raise ConfigError(f"{name} must be an integer")

    drm = str(parsed["drm_device"])
    connector = str(parsed["connector"])
    mode_match = MODE_PATTERN.fullmatch(str(parsed["mode"]))
    if not DRM_PATTERN.fullmatch(drm):
        raise ConfigError("drm_device must match /dev/dri/cardN")
    if not CONNECTOR_PATTERN.fullmatch(connector):
        raise ConfigError("connector has an invalid DRM connector name")
    if not mode_match:
        raise ConfigError("mode must match WIDTHxHEIGHT@MILLIHERTZ")
    width, height, refresh = map(int, mode_match.groups())
    if width > 16_384 or height > 16_384 or refresh > 1_000_000:
        raise ConfigError("mode exceeds bounded dimensions or refresh")
    if not TTY_PATTERN.fullmatch(str(parsed["tty"])):
        raise ConfigError("tty must match /dev/ttyN with N greater than zero")
    for name in ("keyboard_device", "pointer_device"):
        if not INPUT_PATTERN.fullmatch(str(parsed[name])):
            raise ConfigError(f"{name} must match /dev/input/eventN")
    if parsed["keyboard_device"] == parsed["pointer_device"]:
        raise ConfigError("keyboard_device and pointer_device must be distinct")
    card_index = DRM_PATTERN.fullmatch(drm).group(1)  # type: ignore[union-attr]
    expected_debugfs = f"/sys/kernel/debug/dri/{card_index}/{connector}"
    if parsed["debugfs_connector_path"] != expected_debugfs:
        raise ConfigError(f"debugfs_connector_path must equal {expected_debugfs}")
    if not EDID_PATTERN.fullmatch(str(parsed["edid_sha256"])):
        raise ConfigError("edid_sha256 must be exactly 64 lowercase hex digits")
    if len(str(parsed["monitor_model"])) > 128:
        raise ConfigError("monitor_model exceeds 128 characters")
    minimum = int(parsed["expected_min_refresh_hz"])
    maximum = int(parsed["expected_max_refresh_hz"])
    target = int(parsed["target_refresh_hz"])
    if not (1 <= minimum < maximum <= 1000):
        raise ConfigError("expected refresh range must satisfy 1 <= min < max <= 1000")
    if not minimum <= target <= maximum:
        raise ConfigError("target_refresh_hz must lie inside the expected range")
    if not target_distinguishes_fixed_refresh(parsed):
        raise ConfigError("target cadence coincides with fixed-refresh quantization")
    return parsed


def target_distinguishes_fixed_refresh(config: dict[str, object]) -> bool:
    match = MODE_PATTERN.fullmatch(str(config["mode"]))
    assert match
    fixed_interval = 1_000_000_000_000.0 / int(match.group(3))
    target_interval = 1_000_000_000.0 / int(config["target_refresh_hz"])
    tolerance = interval_tolerance(target_interval)
    nearest_multiple = max(1, round(target_interval / fixed_interval))
    return abs(target_interval - nearest_multiple * fixed_interval) > tolerance


def _modetest_has_selected_mode(output: str, connector: str, mode: str) -> bool:
    """Match the exact configured geometry and refresh in one connector block."""
    selected = MODE_PATTERN.fullmatch(mode)
    if selected is None:
        return False
    expected_width, expected_height, expected_millihertz = map(int, selected.groups())
    connector_row = re.compile(
        r"^\s*\d+\s+\d+\s+(?:connected|disconnected|unknown)\s+(\S+)\b",
        re.IGNORECASE,
    )
    mode_row = re.compile(
        r'^\s*(?:#\d+\s+)?"?([1-9][0-9]*)x([1-9][0-9]*)"?\s+'
        r"([0-9]+(?:\.[0-9]+)?)\b"
    )
    in_selected_connector = False
    in_modes = False
    for line in output.splitlines():
        connector_match = connector_row.match(line)
        if connector_match:
            in_selected_connector = connector_match.group(1) == connector
            in_modes = False
            continue
        if not in_selected_connector:
            continue
        heading = line.strip().lower()
        if heading == "modes:":
            in_modes = True
            continue
        if heading == "props:":
            in_modes = False
            continue
        if not in_modes:
            continue
        candidate = mode_row.match(line)
        if candidate is None:
            continue
        width, height = map(int, candidate.groups()[:2])
        try:
            millihertz = Decimal(candidate.group(3)) * 1000
        except InvalidOperation:
            continue
        if (width == expected_width and height == expected_height and
                millihertz == expected_millihertz):
            return True
    return False


def _modetest_connector_property_value(
        output: str, connector: str, property_name: str) -> int | None:
    """Return one integer property value from exactly one connector block."""
    connector_row = re.compile(
        r"^\s*\d+\s+\d+\s+(?:connected|disconnected|unknown)\s+(\S+)\b",
        re.IGNORECASE,
    )
    property_row = re.compile(r"^\s*\d+\s+([A-Za-z0-9_-]+):\s*$")
    value_row = re.compile(r"^\s*value:\s*(-?[0-9]+)\s*$", re.IGNORECASE)
    in_selected_connector = False
    in_properties = False
    selected_property = False
    values: list[int] = []
    for line in output.splitlines():
        candidate = connector_row.match(line)
        if candidate:
            in_selected_connector = candidate.group(1) == connector
            in_properties = False
            selected_property = False
            continue
        if not in_selected_connector:
            continue
        heading = line.strip().lower()
        if heading == "props:":
            in_properties = True
            selected_property = False
            continue
        if not in_properties:
            continue
        candidate_property = property_row.match(line)
        if candidate_property:
            selected_property = candidate_property.group(1) == property_name
            continue
        if not selected_property:
            continue
        candidate_value = value_row.match(line)
        if candidate_value:
            values.append(int(candidate_value.group(1)))
            selected_property = False
    return values[0] if len(values) == 1 else None


def _validate_doctor_facts(config: dict[str, object], facts: dict[str, Any]) -> tuple[bool, list[dict[str, object]]]:
    if set(facts) != DOCTOR_FACT_KEYS:
        raise HarnessError("doctor fixture has a non-exact schema")
    if facts["schema"] != ARTIFACT_SCHEMA:
        raise HarnessError("doctor fixture schema is unsupported")
    expected = {
        "drm_device": config["drm_device"], "tty": config["tty"],
        "connector": config["connector"], "edid_sha256": config["edid_sha256"],
        "mode": config["mode"], "minimum_refresh_hz": config["expected_min_refresh_hz"],
        "maximum_refresh_hz": config["expected_max_refresh_hz"],
        "keyboard_device": config["keyboard_device"],
        "pointer_device": config["pointer_device"],
    }
    checks: list[tuple[str, bool, object]] = []
    for name, value in expected.items():
        checks.append((f"exact {name}", facts.get(name) == value, facts.get(name)))
    for name in ("root", "drm_primary_node", "tty_character_device", "spare_tty",
                 "connected", "atomic_kms", "vrr_enabled_property",
                 "selected_mode_available",
                 "no_competing_drm_master", "session_permissions",
                 "keyboard_character_device", "pointer_character_device"):
        checks.append((name.replace("_", " "), facts.get(name) is True, facts.get(name)))
    checks.append(("vrr capable", facts.get("vrr_capable") == 1, facts.get("vrr_capable")))
    checks.append(("reviewed range source", facts.get("range_source") in {"debugfs", "config-reviewed"}, facts.get("range_source")))
    checks.append(("target cadence distinguishes fixed refresh", target_distinguishes_fixed_refresh(config), config["target_refresh_hz"]))
    for field in ("kernel", "libdrm", "driver", "firmware"):
        value = facts.get(field)
        checks.append((f"recorded {field}", isinstance(value, str) and bool(value), value))
    records = [{"check": label, "passed": ok, "value": detail} for label, ok, detail in checks]
    return all(ok for _, ok, _ in checks), records


def _live_doctor_facts(config: dict[str, object]) -> dict[str, Any]:
    drm = Path(str(config["drm_device"]))
    tty = Path(str(config["tty"]))
    card = drm.name
    connector = str(config["connector"])
    root = Path("/sys/class/drm") / f"{card}-{connector}"
    def is_char(path: Path) -> bool:
        try:
            return stat.S_ISCHR(path.stat().st_mode)
        except OSError:
            return False
    def text(path: Path, fallback: str = "unavailable") -> str:
        try:
            return path.read_text(encoding="ascii").strip() or fallback
        except OSError:
            return fallback
    try:
        edid = (root / "edid").read_bytes()
        digest = hashlib.sha256(edid).hexdigest() if edid else ""
    except OSError:
        digest = ""
    debug = Path(str(config["debugfs_connector_path"]))
    debug_text = text(root / "vrr_capable", "")
    if debug.is_file():
        debug_text += "\n" + text(debug, "")
    elif debug.is_dir() and not debug_text:
        debug_text = text(debug / "vrr_capable", "")
    debug_vrr_capable = 1 if re.search(r"(?:vrr_capable\s*[:=]?\s*)1\b", debug_text, re.I) or debug_text == "1" else 0
    modetest = next((str(path) for path in (Path("/usr/bin/modetest"), Path("/bin/modetest"))
                       if path.is_file() and os.access(path, os.X_OK)), None)
    modetest_text = ""
    if modetest and os.path.realpath(modetest).startswith(("/usr/bin/", "/bin/")):
        result = subprocess.run([modetest, "-D", str(drm), "-c", "-p"], check=False,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=10)
        if result.returncode == 0:
            modetest_text = result.stdout[:1024 * 1024]
    atomic = "atomic" in modetest_text.lower() or "VRR_ENABLED" in modetest_text
    vrr_property = "VRR_ENABLED" in modetest_text
    connector_vrr_capable = _modetest_connector_property_value(
        modetest_text, connector, "vrr_capable")
    vrr_capable = (connector_vrr_capable if connector_vrr_capable in {0, 1}
                   else debug_vrr_capable)
    driver_link = Path("/sys/class/drm") / card / "device" / "driver"
    try:
        driver = driver_link.resolve(strict=True).name
    except OSError:
        driver = "unavailable"
    libdrm = "unavailable"
    pkg_config = next((str(path) for path in (Path("/usr/bin/pkg-config"), Path("/bin/pkg-config"))
                       if path.is_file() and os.access(path, os.X_OK)), None)
    if pkg_config and os.path.realpath(pkg_config).startswith(("/usr/bin/", "/bin/")):
        result = subprocess.run([pkg_config, "--modversion", "libdrm"], check=False,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, timeout=5)
        if result.returncode == 0:
            libdrm = result.stdout.strip() or libdrm
    clients_path = Path("/sys/kernel/debug/dri") / DRM_PATTERN.fullmatch(str(drm)).group(1) / "clients"  # type: ignore[union-attr]
    clients_available = clients_path.is_file() and os.access(clients_path, os.R_OK)
    clients = text(clients_path, "") if clients_available else ""
    competing = any(re.search(r"\by\b", line, re.I) for line in clients.splitlines()[1:])
    current_tty = ""
    try:
        current_tty = os.ttyname(sys.stdin.fileno())
    except OSError:
        pass
    return {
        "schema": ARTIFACT_SCHEMA, "root": os.geteuid() == 0,
        "drm_device": str(drm), "drm_primary_node": is_char(drm),
        "tty": str(tty), "tty_character_device": is_char(tty),
        "spare_tty": is_char(tty) and current_tty == str(tty),
        "connector": connector, "connected": text(root / "status", "") == "connected",
        "edid_sha256": digest, "vrr_capable": vrr_capable,
        "atomic_kms": atomic, "vrr_enabled_property": vrr_property,
        "mode": str(config["mode"]),
        "selected_mode_available": _modetest_has_selected_mode(
            modetest_text, connector, str(config["mode"])),
        "range_source": "debugfs" if debug_text else "config-reviewed",
        "minimum_refresh_hz": config["expected_min_refresh_hz"],
        "maximum_refresh_hz": config["expected_max_refresh_hz"],
        "no_competing_drm_master": clients_available and not competing,
        "session_permissions": os.access(drm, os.R_OK | os.W_OK),
        "kernel": os.uname().release, "libdrm": libdrm, "driver": driver,
        "firmware": text(Path("/sys/class/drm") / card / "device" / "firmware_node"),
        "keyboard_device": config["keyboard_device"],
        "pointer_device": config["pointer_device"],
        "keyboard_character_device": is_char(Path(str(config["keyboard_device"]))),
        "pointer_character_device": is_char(Path(str(config["pointer_device"]))),
    }


def doctor(config_path: Path, fixture_dir: Path | None = None,
           artifact_dir: Path | None = None) -> int:
    try:
        config = parse_config(config_path)
        facts = _read_json(fixture_dir / "doctor.json") if fixture_dir else _live_doctor_facts(config)
        passed, checks = _validate_doctor_facts(config, facts)
        report = {"schema": ARTIFACT_SCHEMA, "passed": passed, "checks": checks,
                  "facts": facts}
        if artifact_dir:
            artifact_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
            _write_json(artifact_dir / "milestone14-hardware-doctor.json", report)
            _write_json(artifact_dir / "milestone14-hardware-config.json", config)
            capability = {key: facts[key] for key in (
                "schema", "drm_device", "driver", "connector", "mode",
                "selected_mode_available", "vrr_capable", "atomic_kms",
                "vrr_enabled_property", "range_source",
                "minimum_refresh_hz", "maximum_refresh_hz")}
            _write_json(artifact_dir / "milestone14-drm-capability.json", capability)
        for check in checks:
            print(f"[{'ok' if check['passed'] else 'failed'}] {check['check']}: {check['value']}")
        print(f"[result] hardware doctor {'passed' if passed else 'failed'}")
        return 0 if passed else 1
    except (HarnessError, OSError, subprocess.SubprocessError) as error:
        print(f"[failed] hardware doctor: {error}", file=sys.stderr)
        return 1
