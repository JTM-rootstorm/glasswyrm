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


def official_window_requests(client: int) -> list[dict]:
    return [
        request(client, 1, name="CreateWindow"),
        request(client, 18, name="ChangeProperty"),
        request(client, 8, name="MapWindow"),
    ]


def main() -> int:
    module = load_module()
    queries = [
        request(7, 98, extension=name) for name in
        ("BIG-REQUESTS", "OTHER", "RANDR", "MIT-SHM")
    ]
    xfixes_probe = [request(9, 98, extension="XFIXES"), *[
        request(9, 130, extension="XFIXES", minor=minor)
        for minor in (0, 2, 5, 13, 14, 15, 17, 18, 19)
    ]]
    mit_shm_probe = [request(10, 129, extension="MIT-SHM", minor=0)]
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-trace-") as temporary:
        root = pathlib.Path(temporary)
        shm = root / "shm.jsonl"
        no_shm = root / "no-shm.jsonl"
        write(shm, [*official_window_requests(7), *queries, *[
            request(7, 129, extension="MIT-SHM", minor=minor, length=length)
            for minor, length in ((1, 16), (3, 40), (2, 8))
        ], *xfixes_probe, *mit_shm_probe])
        write(no_shm, [*official_window_requests(7), *queries,
                       request(7, 72, length=4096),
                       request(8, 72, length=4 * 1024 * 1024), *xfixes_probe])
        result = module.validate(shm, no_shm)
        if not result["passed"]:
            return 1
        if not all(result["independent_extension_coverage"].values()):
            return 1

        write(shm, [*official_window_requests(7), *queries, *[
            request(7, 129, extension="MIT-SHM", minor=minor, length=length)
            for minor, length in ((1, 16), (3, 40), (2, 8))
        ], *mit_shm_probe])
        result = module.validate(shm, no_shm)
        if result["passed"] or "xfixes_probe_in_shm_trace" not in result["evidence_errors"]:
            return 1

        queries_with_xfixes = queries + [request(7, 98, extension="XFIXES")]
        write(shm, [*official_window_requests(7), *queries_with_xfixes, *[
            request(7, 129, extension="MIT-SHM", minor=minor, length=length)
            for minor, length in ((1, 16), (3, 40), (2, 8))
        ], *xfixes_probe, *mit_shm_probe])
        try:
            module.validate(shm, no_shm)
        except ValueError:
            pass
        else:
            return 1

        wrong_order_queries = [queries[1], queries[0], *queries[2:]]
        write(shm, [*official_window_requests(7), *wrong_order_queries, *[
            request(7, 129, extension="MIT-SHM", minor=minor, length=length)
            for minor, length in ((1, 16), (3, 40), (2, 8))
        ], *xfixes_probe, *mit_shm_probe])
        try:
            module.validate(shm, no_shm)
        except ValueError:
            pass
        else:
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
