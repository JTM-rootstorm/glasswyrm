#!/usr/bin/env python3

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


requests = {name: 1 for name in (
    "ChangeWindowAttributes", "CreateGlyphCursor", "FreeCursor",
    "RecolorCursor", "GetSelectionOwner", "SetSelectionOwner",
    "ConvertSelection", "SendEvent")}
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
    "request_histogram": requests,
    "event_histogram": events,
    "event_sequence": sequence,
    "error_histogram": {},
    "unknown_opcodes": [],
    "trace_gated_requests": {"GrabButton": 1, "UngrabButton": 1},
}

with tempfile.TemporaryDirectory() as temporary:
    directory = pathlib.Path(temporary)
    trace = directory / "xterm.trace.json"
    trace.write_text(json.dumps(fixture, sort_keys=True, indent=2) + "\n",
                     encoding="utf-8")
    digest = hashlib.sha256(trace.read_bytes()).hexdigest()
    sums = directory / "SHA256SUMS"
    sums.write_text(f"{digest}  xterm.trace.json\n", encoding="ascii")
    subprocess.run([sys.argv[1], directory], check=True)

    trace.write_text(trace.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    result = subprocess.run([sys.argv[1], directory], capture_output=True,
                            text=True, check=False)
    assert result.returncode != 0
    assert "checksum mismatch" in result.stderr
