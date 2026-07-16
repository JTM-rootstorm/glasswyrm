#!/usr/bin/env python3

import copy
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


requests = {name: 1 for name in (
    "ChangeWindowAttributes", "CreateGlyphCursor", "FreeCursor",
    "RecolorCursor", "GetSelectionOwner", "SetSelectionOwner",
    "ConvertSelection", "SendEvent", "GrabButton", "UngrabButton")}
events = {str(event_type): count for event_type, count in (
    (2, 3), (3, 1), (4, 4), (5, 4), (6, 1), (22, 1), (28, 1),
    (29, 1), (30, 1), (31, 1), (33, 1))}
sequence = [
    {"client": 1, "event_type": event_type}
    for event_type, count in ((int(name), count) for name, count in events.items())
    for _ in range(count)
]
fixture = {
    "schema": 1,
    "first_request_occurrence": list(requests),
    "request_histogram": requests,
    "opcode_histogram": {str(index): 1
                         for index in range(1, len(requests) + 1)},
    "event_histogram": events,
    "event_sequence": sequence,
    "error_histogram": {},
    "reply_requests": ["GetSelectionOwner"],
    "recurring_requests": [],
    "extension_queries": [],
    "unknown_opcodes": [],
    "trace_gated_requests": {"GrabButton": 1, "UngrabButton": 1},
    "application_connection_count": 1,
    "maximum_request_length": 24,
}


def write_fixture(directory, value):
    trace = directory / "xterm.trace.json"
    trace.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n",
                     encoding="utf-8")
    digest = hashlib.sha256(trace.read_bytes()).hexdigest()
    (directory / "SHA256SUMS").write_text(
        f"{digest}  xterm.trace.json\n", encoding="ascii")
    return trace


def rejected(directory, value, message):
    write_fixture(directory, value)
    result = subprocess.run([sys.argv[1], directory], capture_output=True,
                            text=True, check=False)
    assert result.returncode != 0
    assert message in result.stderr, result.stderr


with tempfile.TemporaryDirectory() as temporary:
    directory = pathlib.Path(temporary)
    trace = write_fixture(directory, fixture)
    subprocess.run([sys.argv[1], directory], check=True)

    trace.write_text(trace.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    result = subprocess.run([sys.argv[1], directory], capture_output=True,
                            text=True, check=False)
    assert result.returncode != 0
    assert "checksum mismatch" in result.stderr

    changed = copy.deepcopy(fixture)
    changed["extra"] = 1
    rejected(directory, changed, "unexpected or missing fields")

    changed = copy.deepcopy(fixture)
    changed["trace_gated_requests"]["GrabButton"] = 2
    rejected(directory, changed,
             "trace_gated_requests does not match request_histogram")

    changed = copy.deepcopy(fixture)
    changed["trace_gated_requests"] = {}
    rejected(directory, changed,
             "trace_gated_requests does not match request_histogram")

    changed = copy.deepcopy(fixture)
    changed["opcode_histogram"]["1"] = 2
    rejected(directory, changed,
             "opcode_histogram does not match request_histogram")

    changed = copy.deepcopy(fixture)
    changed["first_request_occurrence"].pop()
    rejected(directory, changed,
             "first_request_occurrence does not match request_histogram")

    changed = copy.deepcopy(fixture)
    changed["reply_requests"] = ["UnobservedRequest"]
    rejected(directory, changed, "reply_requests contains an unobserved request")

    changed = copy.deepcopy(fixture)
    changed["application_connection_count"] = True
    rejected(directory, changed, "invalid application_connection_count")

    changed = copy.deepcopy(fixture)
    changed["maximum_request_length"] = 6
    rejected(directory, changed, "invalid maximum_request_length")
