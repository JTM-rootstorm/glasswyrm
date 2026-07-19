#!/usr/bin/env python3
"""Validate checksum-protected deterministic Milestone 14 host fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


REQUIRED = {
    "README.md",
    "gw-vrr-big.json",
    "gw-vrr-little.json",
    "gwinfo-vrr.json",
    "gwout-vrr-result.json",
    "m14-vrr-client.json",
    "vrr-reasons.json",
    "vrr-state-always.json",
    "vrr-state-app-requested.json",
    "vrr-state-focused.json",
    "vrr-state-fullscreen.json",
    "vrr-state-off.json",
}
SHA_LINE = re.compile(r"^([0-9a-f]{64})  ([^/]+)$")


def fail(message: str) -> None:
    raise ValueError(message)


def load(root: Path, name: str) -> dict[str, object]:
    value = json.loads((root / name).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        fail(f"{name} must contain a JSON object")
    return value


def validate(root: Path) -> None:
    sums = root / "SHA256SUMS"
    if not sums.is_file():
        fail("SHA256SUMS is missing")
    files = {path.name for path in root.iterdir()
             if path.is_file() and path.name != "SHA256SUMS"}
    if not REQUIRED <= files:
        fail("required deterministic host fixture is missing")
    listed: set[str] = set()
    for number, line in enumerate(
            sums.read_text(encoding="ascii").splitlines(), 1):
        match = SHA_LINE.fullmatch(line)
        if not match:
            fail(f"invalid SHA256SUMS line {number}")
        expected, name = match.groups()
        if name in listed or name not in files:
            fail(f"invalid checksum inventory entry {name}")
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            fail(f"checksum mismatch for {name}")
        listed.add(name)
    if listed != files:
        fail("SHA256SUMS must protect every M14 fixture")

    for byte_order in ("little", "big"):
        value = load(root, f"gw-vrr-{byte_order}.json")
        if (value.get("schema") != "glasswyrm.m14-gw-vrr-wire.v1"
                or value.get("byte_order") != byte_order
                or value.get("selected_event_mask") != 7
                or [item.get("requested") for item in
                    value.get("preference_transitions", [])
                    if isinstance(item, dict)]
                != ["default", "allow", "prefer", "disable"]):
            fail(f"GW_VRR {byte_order} fixture differs")

    expected_policies = {
        "vrr-state-off.json": 1,
        "vrr-state-fullscreen.json": 2,
        "vrr-state-focused.json": 3,
        "vrr-state-app-requested.json": 4,
        "vrr-state-always.json": 5,
    }
    for name, policy in expected_policies.items():
        value = load(root, name)
        if (value.get("schema") != "glasswyrm.m14-vrr-state.v1"
                or value.get("policy") != policy):
            fail(f"{name} policy fixture differs")
    app = load(root, "vrr-state-app-requested.json")
    app_states = [item.get("desired_enabled") for item in
                  app.get("preference_transitions", [])
                  if isinstance(item, dict)]
    if app_states != [False, True, False]:
        fail("app-requested preference sequence must remain off/on/off")
    focused = load(root, "vrr-state-focused.json")
    if focused.get("candidate_transitions") != [41, 42]:
        fail("focused policy must transfer the candidate")
    fullscreen = load(root, "vrr-state-fullscreen.json")
    classifications = [item.get("classification") for item in
                       fullscreen.get("transitions", [])
                       if isinstance(item, dict)]
    if classifications != ["fullscreen", "borderless-fullscreen"]:
        fail("fullscreen fixture must include exact borderless classification")

    reasons = load(root, "vrr-reasons.json")
    entries = reasons.get("reasons")
    if (reasons.get("known_mask") != "0x00000001ffffffff"
            or not isinstance(entries, list) or len(entries) != 33
            or [item.get("bit") for item in entries
                if isinstance(item, dict)] != list(range(33))):
        fail("stable VRR reason registry differs")

    gwinfo = load(root, "gwinfo-vrr.json")
    if (len(gwinfo.get("vrr", [])) != 2
            or len(gwinfo.get("windows", [])) != 1):
        fail("gwinfo VRR fixture has incomplete output/window inventory")
    gwout = load(root, "gwout-vrr-result.json")
    acknowledgement = gwout.get("acknowledgement")
    state = gwout.get("state")
    output_states = state.get("vrr") if isinstance(state, dict) else None
    if (not isinstance(acknowledgement, dict)
            or acknowledgement.get("result") != 1
            or acknowledgement.get("applied_generation") != 2
            or not isinstance(state, dict)
            or not isinstance(output_states, list) or len(output_states) != 1
            or not isinstance(output_states[0], dict)
            or output_states[0].get("policy") != "fullscreen"):
        fail("gwout VRR fixture is not an accepted fullscreen policy commit")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    arguments = parser.parse_args()
    try:
        validate(arguments.root)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"m14 host fixtures: {error}", file=sys.stderr)
        return 1
    print("m14 host fixtures: valid deterministic evidence (not hardware proof)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
