# Milestone 11 Source Layout Audit

Status: current repository gate passes; repeat after final Milestone 11 edits.

The M10 exception for `src/ipc/connection.cpp` has been removed. The public C
ABI/lifecycle shell is now 70 lines, with private work divided into handshake,
transport, polling, queues, validation, and correlation modules. Current IPC
implementation files are each below 600 lines; the largest is
`connection_transport.cpp` at 457 lines and correlation is 414 lines.

`tests/tools/source_layout_test.sh` now classifies material changes against the
M10 merge baseline `9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0`. It preserves
the 600-line new/material rewrite limit, 500-line coordinator limit, 250-line
`main.cpp` limit, 100-line function target, and review above 150 lines.
`docs/maintenance/source_size_allowlist.txt` contains no active exception.

M11 responsibilities are separated across `src/input` (device access,
libinput conversion, xkb state, repeat, cursor model), focused server request
handlers and stores (cursor, grabs, selections, keyboard control),
`src/wm/interactive_policy.*`, `src/gwcomp/session_state_coordinator.*`, and
the standalone session launcher. No new M11 source file is allowlisted.

The current `tests/tools/source_layout_test.sh` run passes with eight advisory
function-review findings and no file-budget or allowlist failure. The line
counts above describe the current working implementation; repeat the gate
after all M11 edits settle.
