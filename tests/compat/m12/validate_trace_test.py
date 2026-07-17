#!/usr/bin/env python3
"""Focused success and rejection coverage for live M12 trace validation."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import tempfile


def load_module():
    path = pathlib.Path(__file__).with_name("validate_trace.py")
    spec = importlib.util.spec_from_file_location("m12_validate_trace", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load trace validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path: pathlib.Path, records: list[dict]) -> None:
    path.write_text("".join(json.dumps(record) + "\n" for record in records))


def request(client: int, opcode: int, **fields) -> dict:
    return {
        "direction": "request", "client": client, "opcode": opcode,
        "length": fields.pop("length", 4), "outcome": "success",
        "error": None, **fields,
    }


def main() -> int:
    module = load_module()
    queries = [
        request(7, 98, extension=name) for name in
        ("BIG-REQUESTS", "MIT-SHM", "XFIXES", "RANDR")
    ]
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-trace-") as temporary:
        root = pathlib.Path(temporary)
        shm = root / "shm.jsonl"
        no_shm = root / "no-shm.jsonl"
        write(shm, [request(7, 1), *queries, *[
            request(7, 129, extension="MIT-SHM", minor=minor, length=length)
            for minor, length in ((0, 4), (1, 16), (3, 40), (2, 8))
        ]])
        write(no_shm, [request(7, 1), *queries, request(7, 72, length=4096),
                       request(8, 72, length=4 * 1024 * 1024)])
        result = module.validate(shm, no_shm)
        if not result["passed"]:
            return 1
        broken = json.loads(no_shm.read_text().splitlines()[0])
        write(no_shm, [broken])
        try:
            module.validate(shm, no_shm)
        except ValueError:
            pass
        else:
            return 1
    print("M12 trace validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
