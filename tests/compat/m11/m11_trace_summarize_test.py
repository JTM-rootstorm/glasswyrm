#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys
import tempfile


records = [
    {"direction": "connection", "client": 7, "outcome": "accepted"},
    {"direction": "request", "client": 7, "sequence": 1, "opcode": 98,
     "name": "QueryExtension", "length": 16, "outcome": "success",
     "error": None, "extension": "XInputExtension"},
    {"direction": "reply", "client": 7, "sequence": 1},
    {"direction": "request", "client": 7, "sequence": 2, "opcode": 98,
     "name": "QueryExtension", "length": 20, "outcome": "success",
     "error": None, "extension": "BIG-REQUESTS"},
    {"direction": "request", "client": 7, "sequence": 3, "opcode": 98,
     "name": "QueryExtension", "length": 16, "outcome": "success",
     "error": None, "extension": "MIT-SHM"},
    {"direction": "request", "client": 7, "sequence": 4, "opcode": 98,
     "name": "QueryExtension", "length": 16, "outcome": "success",
     "error": None, "extension": "Composite"},
    {"direction": "request", "client": 7, "sequence": 5, "opcode": 98,
     "name": "QueryExtension", "length": 12, "outcome": "success",
     "error": None, "extension": "RANDR"},
    {"direction": "request", "client": 7, "sequence": 6, "opcode": 98,
     "name": "QueryExtension", "length": 20, "outcome": "success",
     "error": None, "extension": "OTHER"},
    {"direction": "request", "client": 7, "sequence": 7, "opcode": 38,
     "name": "QueryPointer", "length": 8, "outcome": "success",
     "error": None},
    {"direction": "request", "client": 7, "sequence": 8, "opcode": 38,
     "name": "QueryPointer", "length": 8, "outcome": "success",
     "error": None},
    {"direction": "request", "client": 7, "sequence": 9, "opcode": 76,
     "name": "ImageText8", "length": 12, "outcome": "success",
     "error": None},
    {"direction": "request", "client": 7, "sequence": 10, "opcode": 76,
     "name": "ImageText8", "length": 40, "outcome": "success",
     "error": None},
    {"direction": "event", "client": 7, "sequence": 3,
     "event_type": 2, "window": 0x200001},
    {"direction": "event", "client": 7, "sequence": 3,
     "event_type": 2, "window": 0x200001},
    {"direction": "request", "client": 7, "sequence": 11, "opcode": 28,
     "name": "GrabButton", "length": 24, "outcome": "success",
     "error": None},
    {"direction": "request", "client": 7, "sequence": 12, "opcode": 250,
     "name": "Unknown", "length": 12, "outcome": "error",
     "error": "BadRequest"},
    {"direction": "connection", "client": 7, "outcome": "disconnected"},
]

with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "raw.jsonl"
    path.write_text("".join(json.dumps(record) + "\n" for record in records),
                    encoding="utf-8")
    output = subprocess.check_output([sys.argv[1], path], text=True)

summary = json.loads(output)
assert summary["presence_normalized_requests"] == [
    "ClearArea", "CopyArea", "ImageText8", "PolyLine", "PutImage"]
assert summary["first_request_occurrence"] == [
    "QueryExtension", "QueryPointer", "ImageText8", "GrabButton", "Unknown"]
assert summary["request_histogram"] == {
    "GrabButton": 1, "ImageText8": 1, "QueryExtension": 6,
    "QueryPointer": 2, "Unknown": 1}
assert summary["opcode_histogram"] == {
    "28": 1, "38": 2, "76": 1, "98": 6, "250": 1}
assert summary["error_histogram"] == {"BadRequest": 1}
assert summary["reply_requests"] == ["QueryExtension"]
assert summary["recurring_requests"] == ["QueryPointer"]
assert summary["extension_queries"] == ["XInputExtension", "OTHER"]
assert summary["unknown_opcodes"] == [250]
assert summary["trace_gated_requests"] == {"GrabButton": 1}
assert summary["event_histogram"] == {"2": 2}
assert summary["event_sequence"] == [
    {"client": 7, "event_type": 2}, {"client": 7, "event_type": 2}]
assert summary["application_connection_count"] == 1
assert summary["maximum_request_length"] == 40

with tempfile.TemporaryDirectory() as directory:
    invalid = pathlib.Path(directory) / "invalid.jsonl"
    invalid.write_text('{"direction":"event","client":true,"event_type":2}\n',
                       encoding="utf-8")
    result = subprocess.run([sys.argv[1], invalid], capture_output=True,
                            text=True, check=False)
    assert result.returncode != 0
    assert "invalid client" in result.stderr
