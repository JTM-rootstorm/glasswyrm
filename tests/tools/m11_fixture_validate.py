#!/usr/bin/env python3
"""Validate the reviewed Milestone 11 xterm trace fixture and checksum."""

import hashlib
import json
import pathlib
import re
import sys


REQUIRED_REQUESTS = frozenset((
    "ChangeWindowAttributes", "CreateGlyphCursor", "FreeCursor",
    "RecolorCursor", "GetSelectionOwner", "SetSelectionOwner",
    "ConvertSelection", "SendEvent",
))
REQUIRED_EVENT_TYPES = frozenset((2, 3, 4, 5, 6, 22, 28, 29, 30, 31, 33))
SHA256_LINE = re.compile(r"^([0-9a-f]{64})  ([^/]+)$")


def fail(message):
    raise SystemExit(f"m11_fixture_validate: {message}")


if len(sys.argv) != 2:
    fail("usage: m11_fixture_validate.py FIXTURE_DIR")

directory = pathlib.Path(sys.argv[1])
trace_path = directory / "xterm.trace.json"
checksum_path = directory / "SHA256SUMS"
for path in (trace_path, checksum_path):
    if not path.is_file():
        fail(f"missing {path.name}")

checksums = {}
for number, line in enumerate(checksum_path.read_text(encoding="ascii").splitlines(), 1):
    match = SHA256_LINE.fullmatch(line)
    if not match:
        fail(f"invalid SHA256SUMS line {number}")
    digest, name = match.groups()
    if name in checksums:
        fail(f"duplicate checksum entry {name}")
    checksums[name] = digest
if set(checksums) != {trace_path.name}:
    fail("SHA256SUMS must contain only xterm.trace.json")
actual = hashlib.sha256(trace_path.read_bytes()).hexdigest()
if actual != checksums[trace_path.name]:
    fail("xterm.trace.json checksum mismatch")

try:
    trace = json.loads(trace_path.read_text(encoding="utf-8"))
except (UnicodeDecodeError, json.JSONDecodeError) as error:
    fail(f"invalid xterm.trace.json: {error}")
if not isinstance(trace, dict) or trace.get("schema") != 1:
    fail("xterm.trace.json must be a schema 1 object")

requests = trace.get("request_histogram")
events = trace.get("event_histogram")
if not isinstance(requests, dict) or not all(
        isinstance(name, str) and isinstance(count, int) and not isinstance(count, bool)
        and count > 0 for name, count in requests.items()):
    fail("invalid request_histogram")
if not isinstance(events, dict) or not all(
        isinstance(name, str) and name.isdigit() and isinstance(count, int)
        and not isinstance(count, bool) and count > 0
        for name, count in events.items()):
    fail("invalid event_histogram")

missing_requests = REQUIRED_REQUESTS - set(requests)
if missing_requests:
    fail(f"missing required requests: {', '.join(sorted(missing_requests))}")
event_types = {int(name) for name in events}
missing_events = REQUIRED_EVENT_TYPES - event_types
if missing_events:
    fail("missing required event types: " +
         ", ".join(str(value) for value in sorted(missing_events)))
if events.get("2", 0) < 3 or events.get("4", 0) < 4 or events.get("5", 0) < 4:
    fail("event histogram lacks required interactive delivery volume")

sequence = trace.get("event_sequence")
if not isinstance(sequence, list) or len(sequence) != sum(events.values()):
    fail("event_sequence length does not match event_histogram")
observed = {}
for record in sequence:
    if not isinstance(record, dict) or set(record) != {"client", "event_type"}:
        fail("invalid event_sequence record")
    client = record["client"]
    event_type = record["event_type"]
    if (isinstance(client, bool) or not isinstance(client, int) or client < 0 or
            isinstance(event_type, bool) or not isinstance(event_type, int)
            or event_type < 0):
        fail("invalid event_sequence value")
    observed[str(event_type)] = observed.get(str(event_type), 0) + 1
if observed != events:
    fail("event_sequence does not match event_histogram")
