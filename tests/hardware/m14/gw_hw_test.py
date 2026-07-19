#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools" / "gw-hw"
CONFIG = Path(__file__).with_name("config.example.toml")


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def main() -> int:
    self_test = run("self-test")
    assert self_test.returncode == 0 and "self-test: ok" in self_test.stdout
    unconfirmed = run("milestone14-vrr-test", "--config", str(CONFIG))
    assert unconfirmed.returncode == 2 and "literal --yes" in unconfirmed.stderr
    doctor = run("doctor", "--config", str(CONFIG))
    assert doctor.returncode != 0
    assert "hardware proof is not claimed" in doctor.stdout
    print("gw-hw command test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
