# `gwinfo` output diagnostics

`gwinfo` is a same-UID client of the `glasswyrmd` output-control socket. It
queries one complete, generation-tagged snapshot through the public
`libgwipc` API and exits nonzero if the snapshot framing, item count, protocol
version, or acknowledgement is invalid.

```sh
gwinfo --socket /run/glasswyrm/control.sock outputs
gwinfo --socket /run/glasswyrm/control.sock windows --json
gwinfo --socket /run/glasswyrm/control.sock all --json
```

Text and JSON output are deterministic and contain no timestamps. Output IDs
are lowercase, fixed-width hexadecimal strings. The M13 server publishes the
output inventory and current layout together with deterministic window
geometry, policy, output membership, preferred scale, client-buffer scale, and
scale-mode records.

`--help` and `--version` do not connect to a running server.
