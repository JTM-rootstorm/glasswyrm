#!/usr/bin/env python3
"""Exercise the guarded M14 NVIDIA VRR probe wrapper without hardware."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests" / "compat" / "m14"))
sys.path.insert(0, str(ROOT / "tests" / "hardware" / "m14"))
from analyze_nvidia_vrr_probe_test import records, write_report  # noqa: E402
from gw_hw_test import make_fixture  # noqa: E402


def command(*arguments: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(ROOT / "tools" / "gw-hw"),
         "milestone14-nvidia-vrr-probe",
         *(str(argument) for argument in arguments)],
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        config, fixture = make_fixture(root)
        facts_path = fixture / "doctor.json"
        facts = json.loads(facts_path.read_text(encoding="utf-8"))
        facts["keyboard_character_device"] = False
        facts["pointer_character_device"] = False
        facts_path.write_text(
            json.dumps(facts, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

        values = records(off_interval=6_944_444)
        values[0]["mode"] = "2560x1440@144000"
        write_report(fixture / "nvidia-vrr-probe.jsonl", values)

        gated_artifacts = root / "gated"
        gated = command(
            "--config", config, "--fixture-dir", fixture,
            "--artifact-dir", gated_artifacts)
        assert gated.returncode == 2
        assert "literal --yes" in gated.stderr
        assert not gated_artifacts.exists()

        artifacts = root / "accepted"
        accepted = command(
            "--config", config, "--fixture-dir", fixture,
            "--artifact-dir", artifacts, "--yes")
        assert accepted.returncode == 0, accepted.stderr
        summary = json.loads((
            artifacts / "milestone14-nvidia-vrr-probe-summary.json"
        ).read_text(encoding="utf-8"))
        assert summary["classification"] == "accepted-probe-distinction"
        run = json.loads((
            artifacts / "milestone14-nvidia-vrr-probe-run.json"
        ).read_text(encoding="utf-8"))
        assert run["passed"] is True
        assert run["restoration_passed"] is True
        assert run["input_devices_required"] is False
        doctor = json.loads((
            artifacts / "milestone14-hardware-doctor.json"
        ).read_text(encoding="utf-8"))
        labels = {check["check"] for check in doctor["checks"]}
        assert "keyboard character device" not in labels
        assert "pointer character device" not in labels

        repeated = command(
            "--config", config, "--fixture-dir", fixture,
            "--artifact-dir", artifacts, "--yes")
        assert repeated.returncode == 1
        assert "private empty" in repeated.stderr

        direct_artifacts = root / "direct"
        direct = command(
            "--config", config, "--artifact-dir", direct_artifacts, "--yes")
        assert direct.returncode == 1
        assert "requires systemd-run --scope" in direct.stderr
        assert not direct_artifacts.exists()

    print("M14 NVIDIA VRR probe runner test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
