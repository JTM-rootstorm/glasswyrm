#!/usr/bin/env python3
"""Normalize the two final official SDL clients from an accepted M12 trace."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


MAXIMUM_TRACE_BYTES = 64 * 1024 * 1024
MAXIMUM_RECORDS = 250_000
OFFICIAL_SDL_EXTENSION_QUERY_ORDER = [
    "BIG-REQUESTS", "OTHER", "RANDR", "MIT-SHM",
]


def read_records(path: pathlib.Path) -> list[dict[str, Any]]:
    if path.stat().st_size > MAXIMUM_TRACE_BYTES:
        raise ValueError("trace exceeds the fixed 64 MiB limit")
    records: list[dict[str, Any]] = []
    for number, line in enumerate(path.read_text(errors="strict").splitlines(), 1):
        if not line.strip():
            continue
        if len(records) >= MAXIMUM_RECORDS:
            raise ValueError("trace exceeds the fixed record limit")
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"trace line {number} is invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"trace line {number} is not an object")
        records.append(record)
    if not records:
        raise ValueError("trace is empty")
    return records


def ordered_unique(values: list[Any]) -> list[Any]:
    result: list[Any] = []
    for value in values:
        if value not in result:
            result.append(value)
    return result


def requests_by_client(records: list[dict[str, Any]]) -> list[list[dict[str, Any]]]:
    accepted = ordered_unique([
        record.get("client")
        for record in records
        if record.get("direction") == "connection"
        and record.get("outcome") == "accepted"
        and isinstance(record.get("client"), int)
    ])
    return [
        [record for record in records
         if record.get("direction") == "request" and record.get("client") == client]
        for client in accepted
    ]


def is_official_sdl_client(requests: list[dict[str, Any]]) -> bool:
    names = {record.get("name") for record in requests}
    extension_queries = ordered_unique([
        record.get("extension")
        for record in requests
        if record.get("opcode") == 98
    ])
    image_transport = any(
        (record.get("extension") == "MIT-SHM" and record.get("minor") == 3)
        or record.get("opcode") == 72
        for record in requests
    )
    return (
        {"CreateWindow", "ChangeProperty", "MapWindow"} <= names
        and extension_queries == OFFICIAL_SDL_EXTENSION_QUERY_ORDER
        and image_transport
    )


def normalize(workload: str, requests: list[dict[str, Any]]) -> dict[str, Any]:
    errors = [
        record for record in requests
        if record.get("outcome") == "error" or record.get("direction") == "error"
    ]
    extension_requests = ordered_unique([
        {"extension": record["extension"], "minor": record.get("minor")}
        for record in requests
        if isinstance(record.get("extension"), str) and record.get("opcode") != 98
    ])
    recurring_images = ordered_unique([
        {
            "transport": "MIT-SHM" if record.get("extension") == "MIT-SHM" else "core",
            "minor": record.get("minor") if record.get("extension") == "MIT-SHM" else None,
            "request_length": record.get("length"),
        }
        for record in requests
        if (record.get("extension") == "MIT-SHM" and record.get("minor") == 3)
        or record.get("opcode") == 72
    ])
    names = [str(record.get("name", "Unknown")) for record in requests]
    return {
        "schema": 1,
        "workload": workload,
        "selection": (
            "penultimate eligible SDL client" if workload == "testdraw2"
            else "final eligible SDL client"
        ),
        "extension_discovery_order": ordered_unique([
            record["extension"] for record in requests
            if record.get("opcode") == 98 and isinstance(record.get("extension"), str)
        ]),
        "extension_requests": extension_requests,
        "core_request_classes": ordered_unique(names),
        "recurring_image_classes": recurring_images,
        "randr_minors": ordered_unique([
            record.get("minor") for record in requests
            if record.get("extension") == "RANDR"
        ]),
        "ewmh_request_classes": [
            name for name in ("ChangeProperty", "GetProperty", "SendEvent")
            if name in names
        ],
        "unexpected_errors": errors,
        "passed": bool(recurring_images) and not errors,
    }


def normalized_official_traces(path: pathlib.Path) -> dict[str, dict[str, Any]]:
    candidates = [
        requests for requests in requests_by_client(read_records(path))
        if is_official_sdl_client(requests)
    ]
    if len(candidates) < 2:
        raise ValueError("trace does not contain the final two eligible SDL clients")
    traces = {
        "testdraw2": normalize("testdraw2", candidates[-2]),
        "testsprite2": normalize("testsprite2", candidates[-1]),
    }
    if not all(value["passed"] for value in traces.values()):
        raise ValueError("official SDL trace contains an unexpected protocol error")
    return traces


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", type=pathlib.Path, required=True)
    parser.add_argument("--testdraw2", type=pathlib.Path, required=True)
    parser.add_argument("--testsprite2", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        traces = normalized_official_traces(arguments.trace)
        arguments.testdraw2.write_text(
            json.dumps(traces["testdraw2"], indent=2, sort_keys=True) + "\n"
        )
        arguments.testsprite2.write_text(
            json.dumps(traces["testsprite2"], indent=2, sort_keys=True) + "\n"
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(f"M12 official trace normalization: {error}")
        return 1
    print("M12 official trace normalization: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
