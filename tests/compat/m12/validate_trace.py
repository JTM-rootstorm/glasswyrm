#!/usr/bin/env python3
"""Validate and normalize the bounded live M12 SDL X11 traces."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


MAXIMUM_TRACE_BYTES = 64 * 1024 * 1024
MAXIMUM_RECORDS = 250_000
SDL_EXTENSIONS = {"BIG-REQUESTS", "MIT-SHM", "XFIXES", "RANDR"}


def read_records(path: pathlib.Path) -> list[dict[str, Any]]:
    if path.stat().st_size > MAXIMUM_TRACE_BYTES:
        raise ValueError(f"{path}: trace exceeds the fixed size limit")
    result: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(errors="strict").splitlines(), 1):
        if not line.strip():
            continue
        if len(result) >= MAXIMUM_RECORDS:
            raise ValueError(f"{path}: trace exceeds the fixed record limit")
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"{path}:{line_number}: record is not an object")
        result.append(record)
    if not result:
        raise ValueError(f"{path}: trace is empty")
    return result


def requests_by_client(records: list[dict[str, Any]]) -> dict[int, list[dict[str, Any]]]:
    clients: dict[int, list[dict[str, Any]]] = {}
    for record in records:
        client = record.get("client")
        if record.get("direction") == "request" and isinstance(client, int):
            clients.setdefault(client, []).append(record)
    return clients


def client_errors(records: list[dict[str, Any]], client: int) -> int:
    return sum(
        record.get("client") == client
        and (
            record.get("direction") == "error"
            or (
                record.get("direction") == "request"
                and record.get("outcome") == "error"
            )
        )
        for record in records
    )


def query_extensions(requests: list[dict[str, Any]]) -> list[str]:
    return [
        str(record["extension"])
        for record in requests
        if record.get("opcode") == 98 and isinstance(record.get("extension"), str)
    ]


def shm_profile(records: list[dict[str, Any]]) -> dict[str, Any]:
    candidates = []
    for client, requests in requests_by_client(records).items():
        opcodes = {record.get("opcode") for record in requests}
        minors = {
            record.get("minor")
            for record in requests
            if record.get("extension") == "MIT-SHM"
        }
        queries = query_extensions(requests)
        if 1 in opcodes and {0, 1, 2, 3} <= minors:
            candidates.append((client, requests, queries))
    if not candidates:
        raise ValueError("SHM trace has no window client with QueryVersion/Attach/PutImage/Detach")
    client, requests, queries = candidates[0]
    checks = {
        "sdl_window_client": True,
        "required_extension_queries": SDL_EXTENSIONS <= set(queries),
        "shm_query_attach_put_detach": True,
        "no_bulk_core_put_image": not any(
            record.get("opcode") == 72 and record.get("length", 0) > 262_140
            for record in requests
        ),
        "no_client_errors": client_errors(records, client) == 0,
    }
    return {
        "client": client,
        "extension_queries": queries,
        "request_count": len(requests),
        "checks": checks,
        "passed": all(checks.values()),
    }


def no_shm_profile(records: list[dict[str, Any]]) -> dict[str, Any]:
    candidates = []
    clients = requests_by_client(records)
    for client, requests in clients.items():
        opcodes = {record.get("opcode") for record in requests}
        queries = query_extensions(requests)
        if 1 in opcodes and 72 in opcodes and "MIT-SHM" in queries:
            candidates.append((client, requests, queries))
    if not candidates:
        raise ValueError("no-SHM trace has no SDL window client using core PutImage")
    client, requests, queries = candidates[0]
    checks = {
        "sdl_window_client": True,
        "mit_shm_queried": "MIT-SHM" in queries,
        "core_put_image": any(record.get("opcode") == 72 for record in requests),
        "no_mit_shm_requests": not any(record.get("opcode") == 129 for record in requests),
        "extended_put_image_probe": any(
            record.get("opcode") == 72 and record.get("length", 0) > 262_140
            for values in clients.values()
            for record in values
        ),
        "no_client_errors": client_errors(records, client) == 0,
    }
    return {
        "client": client,
        "extension_queries": queries,
        "request_count": len(requests),
        "checks": checks,
        "passed": all(checks.values()),
    }


def validate(shm: pathlib.Path, no_shm: pathlib.Path) -> dict[str, Any]:
    profiles = {
        "shm": shm_profile(read_records(shm)),
        "no_shm": no_shm_profile(read_records(no_shm)),
    }
    errors = [name for name, profile in profiles.items() if not profile["passed"]]
    return {
        "schema": 1,
        "profiles": profiles,
        "passed": not errors,
        "evidence_errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shm", type=pathlib.Path, required=True)
    parser.add_argument("--no-shm", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        result = validate(arguments.shm, arguments.no_shm)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"M12 trace validation: {error}")
        return 1
    arguments.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    if not result["passed"]:
        print("M12 trace validation: " + ", ".join(result["evidence_errors"]))
        return 1
    print("M12 trace validation: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
