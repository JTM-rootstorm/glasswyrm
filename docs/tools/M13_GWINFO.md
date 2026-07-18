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
are lowercase, fixed-width hexadecimal strings. The M13 server currently
publishes output inventory and layout records; the client also understands the
window geometry, policy, membership, and scale records reserved by the query
contract as those records become available from the server control service.

`--help` and `--version` do not connect to a running server.
