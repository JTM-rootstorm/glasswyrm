#!/usr/bin/env python3
import json, pathlib, subprocess, sys, tempfile
records = [
 {"direction":"connection","client":1,"outcome":"accepted"},
 {"direction":"request","client":1,"sequence":1,"opcode":98,"name":"QueryExtension","length":16,"outcome":"success","error":None,"extension":"RENDER"},
 {"direction":"reply","client":1,"sequence":1},
 {"direction":"request","client":1,"sequence":2,"opcode":38,"name":"QueryPointer","length":8,"outcome":"success","error":None},
 {"direction":"request","client":1,"sequence":3,"opcode":38,"name":"QueryPointer","length":8,"outcome":"success","error":None},
 {"direction":"request","client":1,"sequence":4,"opcode":250,"name":"Unknown","length":12,"outcome":"error","error":"BadRequest"},
 {"direction":"connection","client":1,"outcome":"disconnected"},
]
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "raw.jsonl"
    path.write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")
    output = subprocess.check_output([sys.argv[1], path], text=True)
summary = json.loads(output)
assert summary["first_request_occurrence"] == ["QueryExtension", "QueryPointer", "Unknown"]
assert summary["recurring_requests"] == ["QueryPointer"]
assert "38" not in summary["opcode_histogram"]
assert summary["extension_queries"] == ["RENDER"]
assert summary["unknown_opcodes"] == [250]
assert summary["reply_requests"] == ["QueryExtension"]
assert summary["application_connection_count"] == 1
assert summary["maximum_request_length"] == 16
