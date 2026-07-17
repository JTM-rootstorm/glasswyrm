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
TRACE_GATED_REQUESTS = frozenset(("GrabButton",))
RECURRING_REQUESTS = frozenset(("QueryPointer", "QueryKeymap"))
PRESENCE_NORMALIZED_OPCODES = {
    "ClearArea": "61",
    "CopyArea": "62",
    "ImageText8": "76",
    "PolyLine": "65",
    "PutImage": "72",
}
PRESENCE_NORMALIZED_REQUESTS = frozenset(PRESENCE_NORMALIZED_OPCODES)
TRACE_KEYS = frozenset((
    "schema", "presence_normalized_requests", "first_request_occurrence",
    "request_histogram",
    "opcode_histogram", "error_histogram", "reply_requests",
    "recurring_requests", "extension_queries", "unknown_opcodes",
    "trace_gated_requests", "event_histogram", "event_sequence",
    "application_connection_count", "maximum_request_length",
))
SHA256_LINE = re.compile(r"^([0-9a-f]{64})  ([^/]+)$")


def fail(message):
    raise SystemExit(f"m11_fixture_validate: {message}")


def positive_histogram(value, name, numeric_keys=False):
    if not isinstance(value, dict) or not all(
            isinstance(key, str) and (not numeric_keys or key.isdigit())
            and isinstance(count, int) and not isinstance(count, bool)
            and count > 0 for key, count in value.items()):
        fail(f"invalid {name}")
    return value


def unique_string_list(value, name):
    if (not isinstance(value, list)
            or not all(isinstance(item, str) for item in value)
            or len(value) != len(set(value))):
        fail(f"invalid {name}")
    return value


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
if set(trace) != TRACE_KEYS:
    fail("xterm.trace.json has unexpected or missing fields")
if trace.get("error_histogram") != {}:
    fail("accepted trace must not contain X11 errors")
if trace.get("unknown_opcodes") != []:
    fail("accepted trace must not contain unknown opcodes")

presence_normalized = unique_string_list(
    trace.get("presence_normalized_requests"),
    "presence_normalized_requests")
if set(presence_normalized) != PRESENCE_NORMALIZED_REQUESTS:
    fail("invalid presence_normalized_requests")

requests = positive_histogram(trace.get("request_histogram"),
                              "request_histogram")
events = positive_histogram(trace.get("event_histogram"),
                            "event_histogram", numeric_keys=True)
opcodes = positive_histogram(trace.get("opcode_histogram"),
                             "opcode_histogram", numeric_keys=True)
if any(int(opcode) > 255 for opcode in opcodes):
    fail("invalid opcode_histogram")
if sum(opcodes.values()) != sum(requests.values()):
    fail("opcode_histogram does not match request_histogram")
if any(requests.get(name) != 1 for name in PRESENCE_NORMALIZED_REQUESTS):
    fail("presence-normalized request count must be one")
if any(opcodes.get(opcode) != 1
       for opcode in PRESENCE_NORMALIZED_OPCODES.values()):
    fail("presence-normalized opcode count must be one")

first_requests = unique_string_list(trace.get("first_request_occurrence"),
                                    "first_request_occurrence")
if set(first_requests) != set(requests):
    fail("first_request_occurrence does not match request_histogram")
reply_requests = unique_string_list(trace.get("reply_requests"),
                                    "reply_requests")
if not set(reply_requests).issubset(requests):
    fail("reply_requests contains an unobserved request")
recurring = unique_string_list(trace.get("recurring_requests"),
                               "recurring_requests")
expected_recurring = sorted(name for name in RECURRING_REQUESTS
                            if name in requests)
if recurring != expected_recurring:
    fail("recurring_requests does not match request_histogram")
extensions = unique_string_list(trace.get("extension_queries"),
                                "extension_queries")
if extensions and "QueryExtension" not in requests:
    fail("extension_queries requires QueryExtension")

trace_gated = positive_histogram(trace.get("trace_gated_requests"),
                                 "trace_gated_requests")
if set(trace_gated) != TRACE_GATED_REQUESTS:
    fail("invalid trace_gated_requests")
expected_trace_gated = {
    name: requests[name] for name in TRACE_GATED_REQUESTS if name in requests
}
if trace_gated != expected_trace_gated:
    fail("trace_gated_requests does not match request_histogram")

missing_requests = REQUIRED_REQUESTS - set(requests)
if missing_requests:
    fail(f"missing required requests: {', '.join(sorted(missing_requests))}")
event_types = {int(name) for name in events}
if any(event_type > 127 for event_type in event_types):
    fail("invalid event_histogram")
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

connection_count = trace.get("application_connection_count")
if (isinstance(connection_count, bool) or not isinstance(connection_count, int)
        or connection_count <= 0):
    fail("invalid application_connection_count")
maximum_request_length = trace.get("maximum_request_length")
if (isinstance(maximum_request_length, bool)
        or not isinstance(maximum_request_length, int)
        or maximum_request_length <= 0 or maximum_request_length % 4 != 0):
    fail("invalid maximum_request_length")
