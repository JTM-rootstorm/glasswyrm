"""Strict configuration parsing and read-only hardware doctor facts."""

from __future__ import annotations

import array
import fcntl
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
    ARTIFACT_SCHEMA, COMMIT_PATTERN, CONNECTOR_PATTERN, ConfigError,
    DOCTOR_FACT_KEYS, DRM_PATTERN, EDID_PATTERN, HarnessError, INPUT_PATTERN,
    INTEGER_KEYS, M14_REQUIRED_BASE_COMMIT, MAX_CONFIG_BYTES, MODE_PATTERN,
    SCHEMA_KEYS, STRING_KEYS, TTY_PATTERN, _read_json, _read_regular,
    _safe_text, _write_json, interval_tolerance,
)
from provenance import validate_build_provenance


MODETEST_MODULE_PATTERN = re.compile(r"[A-Za-z0-9_-]+")


def _modetest_commands(
        executable: str, drm: Path, driver: str) -> list[list[str]]:
    """Return exact-node discovery commands, including a bounded driver fallback."""
    suffix = ["-D", str(drm), "-c", "-e", "-p"]
    commands = [[executable, *suffix]]
    if not MODETEST_MODULE_PATTERN.fullmatch(driver):
        return commands
    module = "nvidia-drm" if driver == "nvidia" else driver
    commands.append([executable, "-M", module, *suffix])
    return commands


def _query_modetest(executable: str, drm: Path, driver: str) -> str:
    """Collect one successful bounded resource dump without changing KMS state."""
    for command in _modetest_commands(executable, drm, driver):
        try:
            result = subprocess.run(
                command, check=False, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                timeout=10,
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        if result.returncode == 0:
            return result.stdout[:1024 * 1024]
    return ""


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
    for name in ("tty", "alternate_tty"):
        if not TTY_PATTERN.fullmatch(str(parsed[name])):
            raise ConfigError(f"{name} must match /dev/ttyN with N greater than zero")
    if parsed["tty"] == parsed["alternate_tty"]:
        raise ConfigError("tty and alternate_tty must be distinct")
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
    if parsed["required_base_commit"] != M14_REQUIRED_BASE_COMMIT:
        raise ConfigError(
            f"required_base_commit must equal {M14_REQUIRED_BASE_COMMIT}")
    if not COMMIT_PATTERN.fullmatch(str(parsed["tested_commit"])):
        raise ConfigError("tested_commit must be exactly 40 lowercase hex digits")
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


def validate_cli_identity(config: dict[str, object], required_base: str,
                          tested_commit: str) -> None:
    if required_base != config["required_base_commit"]:
        raise ConfigError("--required-base does not match the reviewed configuration")
    if tested_commit != config["tested_commit"]:
        raise ConfigError("--tested-commit does not match the reviewed configuration")


def target_distinguishes_fixed_refresh(config: dict[str, object]) -> bool:
    match = MODE_PATTERN.fullmatch(str(config["mode"]))
    assert match
    fixed_interval = 1_000_000_000_000.0 / int(match.group(3))
    target_interval = 1_000_000_000.0 / int(config["target_refresh_hz"])
    tolerance = interval_tolerance(target_interval)
    nearest_multiple = max(1, round(target_interval / fixed_interval))
    return abs(target_interval - nearest_multiple * fixed_interval) > tolerance


def _parse_debugfs_refresh_range(text: str) -> tuple[int, int] | None:
    """Parse an explicitly labelled whole-Hz min/max pair, or fail closed."""
    labels = {
        "minimum": re.compile(
            r"\b(?:min(?:imum)?(?:[_ -]?refresh)?(?:[_ -]?rate)?)\b\s*[:=]?\s*"
            r"([1-9][0-9]{0,3})(?:\s*hz)?\b", re.IGNORECASE),
        "maximum": re.compile(
            r"\b(?:max(?:imum)?(?:[_ -]?refresh)?(?:[_ -]?rate)?)\b\s*[:=]?\s*"
            r"([1-9][0-9]{0,3})(?:\s*hz)?\b", re.IGNORECASE),
    }
    values: dict[str, set[int]] = {name: set() for name in labels}
    for name, pattern in labels.items():
        values[name].update(int(match.group(1)) for match in pattern.finditer(text))
    if len(values["minimum"]) != 1 or len(values["maximum"]) != 1:
        return None
    minimum = next(iter(values["minimum"]))
    maximum = next(iter(values["maximum"]))
    return (minimum, maximum) if 1 <= minimum < maximum <= 1000 else None


def _reviewed_range_source(text: str, minimum: int, maximum: int) -> str:
    return ("debugfs" if _parse_debugfs_refresh_range(text) == (minimum, maximum)
            else "config-reviewed")


def _connector_profile(states: list[tuple[str, str, str]],
                       selected: str) -> tuple[int, int, bool, bool]:
    """Return connected/active counts and the selected connector state."""
    connected = [(name, enabled) for name, status, enabled in states
                 if status == "connected"]
    active = [(name, enabled) for name, enabled in connected if enabled == "enabled"]
    selected_connected = any(name == selected for name, _ in connected)
    selected_active = any(name == selected for name, _ in active)
    return len(connected), len(active), selected_connected, selected_active


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
    in_connectors = False
    values: list[int] = []
    for line in output.splitlines():
        if line == line.lstrip() and line.rstrip().endswith(":"):
            in_connectors = line.strip().lower() == "connectors:"
            in_selected_connector = False
            in_properties = False
            selected_property = False
            continue
        if not in_connectors:
            continue
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


def _modetest_selected_crtc_id(output: str, connector: str) -> int | None:
    connector_row = re.compile(
        r"^\s*\d+\s+(\d+)\s+(?:connected|disconnected|unknown)\s+(\S+)\b",
        re.IGNORECASE,
    )
    encoder_row = re.compile(r"^\s*(\d+)\s+(\d+)\s+\S+\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s*$",
                             re.IGNORECASE)
    section = ""
    encoder_ids: list[int] = []
    encoder_crtcs: list[tuple[int, int]] = []
    for line in output.splitlines():
        if line == line.lstrip() and line.rstrip().endswith(":"):
            section = line.strip().lower()
            continue
        if section == "connectors:":
            candidate = connector_row.match(line)
            if candidate and candidate.group(2) == connector:
                encoder_ids.append(int(candidate.group(1)))
        elif section == "encoders:":
            candidate = encoder_row.match(line)
            if candidate:
                encoder_crtcs.append(
                    (int(candidate.group(1)), int(candidate.group(2))))
    if len(encoder_ids) != 1 or encoder_ids[0] == 0:
        return None
    crtc_ids = [crtc_id for encoder_id, crtc_id in encoder_crtcs
                if encoder_id == encoder_ids[0]]
    if len(crtc_ids) != 1 or crtc_ids[0] == 0:
        return None
    return crtc_ids[0]


def _modetest_crtc_property_value(
        output: str, crtc_id: int, property_name: str) -> int | None:
    crtc_row = re.compile(
        r"^\s*(\d+)\s+\d+\s+\(-?[0-9]+,-?[0-9]+\)\s+"
        r"\([0-9]+x[0-9]+\)\s*$")
    property_row = re.compile(r"^\s*\d+\s+([A-Za-z0-9_-]+):\s*$")
    value_row = re.compile(r"^\s*value:\s*(-?[0-9]+)\s*$", re.IGNORECASE)
    in_selected_crtc = False
    in_properties = False
    selected_property = False
    in_crtcs = False
    values: list[int] = []
    for line in output.splitlines():
        if line == line.lstrip() and line.rstrip().endswith(":"):
            in_crtcs = line.strip().lower() == "crtcs:"
            in_selected_crtc = False
            in_properties = False
            selected_property = False
            continue
        if not in_crtcs:
            continue
        candidate = crtc_row.match(line)
        if candidate:
            in_selected_crtc = int(candidate.group(1)) == crtc_id
            in_properties = False
            selected_property = False
            continue
        if not in_selected_crtc:
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
        "alternate_tty": config["alternate_tty"],
        "active_tty": config["tty"],
        "connector": config["connector"], "edid_sha256": config["edid_sha256"],
        "mode": config["mode"], "minimum_refresh_hz": config["expected_min_refresh_hz"],
        "maximum_refresh_hz": config["expected_max_refresh_hz"],
        "keyboard_device": config["keyboard_device"],
        "pointer_device": config["pointer_device"],
    }
    checks: list[tuple[str, bool, object]] = []
    for name, value in expected.items():
        checks.append((f"exact {name}", facts.get(name) == value, facts.get(name)))
    for name in ("root", "drm_primary_node", "tty_character_device",
                 "tty_kd_text", "spare_tty", "alternate_tty_character_device",
                 "alternate_tty_kd_text", "alternate_tty_safe", "connected",
                 "active", "single_connected_active_connector",
                 "atomic_kms", "vrr_enabled_property",
                 "selected_mode_available",
                 "no_competing_drm_master", "session_permissions",
                 "keyboard_character_device", "pointer_character_device"):
        checks.append((name.replace("_", " "), facts.get(name) is True, facts.get(name)))
    checks.append(("exactly one connected connector",
                   facts.get("connected_connector_count") == 1,
                   facts.get("connected_connector_count")))
    checks.append(("exactly one active connector",
                   facts.get("active_connector_count") == 1,
                   facts.get("active_connector_count")))
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
    alternate_tty = Path(str(config["alternate_tty"]))
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
    def kd_text(path: Path) -> bool:
        try:
            descriptor = os.open(
                path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        except OSError:
            return False
        try:
            mode = array.array("I", [0])
            fcntl.ioctl(descriptor, 0x4B3B, mode, True)  # KDGETMODE
            return mode[0] == 0  # KD_TEXT
        except OSError:
            return False
        finally:
            os.close(descriptor)
    driver_link = Path("/sys/class/drm") / card / "device" / "driver"
    try:
        driver = driver_link.resolve(strict=True).name
    except OSError:
        driver = "unavailable"
    try:
        edid = (root / "edid").read_bytes()
        digest = hashlib.sha256(edid).hexdigest() if edid else ""
    except OSError:
        digest = ""
    debug = Path(str(config["debugfs_connector_path"]))
    debug_text = ""
    if debug.is_file():
        debug_text = text(debug, "")
    elif debug.is_dir():
        for name in ("vrr_range", "vrr_capable"):
            contents = text(debug / name, "")
            if contents:
                debug_text += ("\n" if debug_text else "") + contents
    sysfs_vrr_text = text(root / "vrr_capable", "")
    debug_vrr_capable = 1 if (
        re.search(r"(?:vrr_capable\s*[:=]?\s*)1\b", debug_text, re.I) or
        debug_text == "1" or sysfs_vrr_text == "1") else 0
    reviewed_range = (int(config["expected_min_refresh_hz"]),
                      int(config["expected_max_refresh_hz"]))
    range_source = _reviewed_range_source(debug_text, *reviewed_range)
    modetest = next((str(path) for path in (Path("/usr/bin/modetest"), Path("/bin/modetest"))
                       if path.is_file() and os.access(path, os.X_OK)), None)
    modetest_text = ""
    if modetest and os.path.realpath(modetest).startswith(("/usr/bin/", "/bin/")):
        modetest_text = _query_modetest(modetest, drm, driver)
    selected_crtc_id = _modetest_selected_crtc_id(modetest_text, connector)
    selected_vrr_value = (_modetest_crtc_property_value(
        modetest_text, selected_crtc_id, "VRR_ENABLED")
        if selected_crtc_id is not None else None)
    vrr_property = selected_vrr_value in {0, 1}
    atomic = "atomic" in modetest_text.lower() or vrr_property
    connector_vrr_capable = _modetest_connector_property_value(
        modetest_text, connector, "vrr_capable")
    vrr_capable = (connector_vrr_capable if connector_vrr_capable in {0, 1}
                   else debug_vrr_capable)
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
    active_tty_name = text(Path("/sys/class/tty/tty0/active"), "")
    active_tty = f"/dev/{active_tty_name}" if active_tty_name else ""
    tty_is_text = kd_text(tty)
    alternate_is_text = kd_text(alternate_tty)
    tty_is_active = active_tty == str(tty)
    alternate_is_safe = (is_char(alternate_tty) and alternate_is_text and
                         active_tty != str(alternate_tty) and tty != alternate_tty)
    connector_states: list[tuple[str, str, str]] = []
    for candidate in sorted(Path("/sys/class/drm").glob(f"{card}-*")):
        name = candidate.name.removeprefix(f"{card}-")
        status = text(candidate / "status", "")
        if status not in {"connected", "disconnected", "unknown"}:
            continue
        connector_states.append((name, status, text(candidate / "enabled", "")))
    connected_count, active_count, selected_connected, selected_active = (
        _connector_profile(connector_states, connector))
    return {
        "schema": ARTIFACT_SCHEMA, "root": os.geteuid() == 0,
        "drm_device": str(drm), "drm_primary_node": is_char(drm),
        "tty": str(tty), "tty_character_device": is_char(tty),
        "tty_kd_text": tty_is_text, "active_tty": active_tty,
        "spare_tty": is_char(tty) and tty_is_text and tty_is_active,
        "alternate_tty": str(alternate_tty),
        "alternate_tty_character_device": is_char(alternate_tty),
        "alternate_tty_kd_text": alternate_is_text,
        "alternate_tty_safe": alternate_is_safe,
        "connector": connector, "connected": selected_connected,
        "active": selected_active,
        "connected_connector_count": connected_count,
        "active_connector_count": active_count,
        "single_connected_active_connector": (
            connected_count == 1 and active_count == 1 and selected_active),
        "edid_sha256": digest, "vrr_capable": vrr_capable,
        "atomic_kms": atomic, "vrr_enabled_property": vrr_property,
        "mode": str(config["mode"]),
        "selected_mode_available": _modetest_has_selected_mode(
            modetest_text, connector, str(config["mode"])),
        "range_source": range_source,
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


def doctor(config_path: Path, required_base: str, tested_commit: str,
           fixture_dir: Path | None = None,
           artifact_dir: Path | None = None) -> int:
    try:
        config = parse_config(config_path)
        validate_cli_identity(config, required_base, tested_commit)
        if fixture_dir is None:
            validate_build_provenance(tested_commit, artifact_dir)
            print("[ok] exact physical build provenance")
        facts = _read_json(fixture_dir / "doctor.json") if fixture_dir else _live_doctor_facts(config)
        passed, checks = _validate_doctor_facts(config, facts)
        report = {"schema": ARTIFACT_SCHEMA, "passed": passed,
                  "required_base_commit": config["required_base_commit"],
                  "tested_commit": config["tested_commit"], "checks": checks,
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
