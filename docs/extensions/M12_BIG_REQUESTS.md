# Milestone 12 BIG-REQUESTS Subset

Glasswyrm implements the BIG-REQUESTS 1.0 `Enable` request only. Discovery and
dispatch are available only with `glasswyrmd --game-compat`.

`Enable` is idempotent and changes framing only for the requesting client. Its
reply advertises 4,194,304 four-byte units, matching the enforced 16 MiB
request cap. Before enablement, a zero 16-bit request length keeps the
historical invalid-length behavior. After enablement, zero selects the
additional 32-bit length at bytes 4 through 7; that value includes the extended
length word and must be at least two units.

The incremental framer handles either client byte order, preserves following
pipelined requests, and rejects values above the advertised cap before
dispatch. Large requests charge their complete byte length to the existing
per-client reactor work budget. The accepted fallback workload is a core
`PutImage` larger than the ordinary 65,535-unit request limit; BIG-REQUESTS
does not add new image semantics.

Tests cover enablement, both byte orders, fragmented extended headers,
pipelining, the exact maximum and one-unit-over limit, ordinary two-unit
requests after enablement, and large-request continuation/isolation.
