#!/usr/bin/env python3
"""Check whether an existing Meson build matches one M14 dev profile."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys


PROFILES = {
    "probe": {
        "buildtype": "debugoptimized",
        "werror": True,
        "drm_backend": True,
    },
    "stack": {
        "buildtype": "debugoptimized",
        "werror": True,
        "drm_backend": True,
        "libinput_backend": True,
        "render_gl": True,
    },
}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in PROFILES:
        return 2
    result = subprocess.run(
        ["meson", "introspect", "--buildoptions", str(Path(sys.argv[2]))],
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        return 1
    try:
        records = json.loads(result.stdout)
        observed = {record["name"]: record["value"] for record in records}
    except (KeyError, TypeError, json.JSONDecodeError):
        return 1
    return 0 if all(
        observed.get(name) == value
        for name, value in PROFILES[sys.argv[1]].items()
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
