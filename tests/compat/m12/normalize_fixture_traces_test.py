#!/usr/bin/env python3
"""Focused tests for deterministic M12 official-client trace selection."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import tempfile


def load():
    path = pathlib.Path(__file__).with_name("normalize_fixture_traces.py")
    spec = importlib.util.spec_from_file_location("normalize_fixture_traces", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load fixture trace normalizer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def client(client_id: int, image_length: int, error: bool = False) -> list[dict]:
    records = [
        {"direction": "connection", "client": client_id, "outcome": "accepted"},
    ]
    sequence = 0
    for extension in ("BIG-REQUESTS", "MIT-SHM", "XFIXES", "RANDR"):
        sequence += 1
        records.append({"direction": "request", "client": client_id,
                        "sequence": sequence, "opcode": 98,
                        "name": "QueryExtension", "extension": extension,
                        "length": 16, "outcome": "success", "error": None})
    for opcode, name in ((1, "CreateWindow"), (18, "ChangeProperty"),
                         (8, "MapWindow"), (25, "SendEvent")):
        sequence += 1
        records.append({"direction": "request", "client": client_id,
                        "sequence": sequence, "opcode": opcode, "name": name,
                        "length": 8, "outcome": "success", "error": None})
    sequence += 1
    records.append({"direction": "request", "client": client_id,
                    "sequence": sequence, "opcode": 129, "name": "Unknown",
                    "extension": "MIT-SHM", "minor": 3,
                    "length": image_length,
                    "outcome": "error" if error else "success",
                    "error": "BadLength" if error else None})
    return records


def main() -> int:
    normalizer = load()
    records = client(1, 40) + client(2, 44) + client(3, 48)
    with tempfile.TemporaryDirectory(prefix="glasswyrm-m12-trace-") as temporary:
        trace = pathlib.Path(temporary) / "trace.jsonl"
        trace.write_text("".join(json.dumps(record) + "\n" for record in records))
        normalized = normalizer.normalized_official_traces(trace)
        if normalized["testdraw2"]["recurring_image_classes"][0]["request_length"] != 44:
            return 1
        if normalized["testsprite2"]["recurring_image_classes"][0]["request_length"] != 48:
            return 1
        if normalized["testsprite2"]["ewmh_request_classes"] != ["ChangeProperty", "SendEvent"]:
            return 1
        trace.write_text("".join(
            json.dumps(record) + "\n"
            for record in client(1, 40) + client(2, 44) + client(3, 48, True)
        ))
        try:
            normalizer.normalized_official_traces(trace)
        except ValueError:
            pass
        else:
            return 1
    print("M12 official trace normalizer tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
