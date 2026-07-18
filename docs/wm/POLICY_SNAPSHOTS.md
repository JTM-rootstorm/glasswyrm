# Window Policy Snapshots

Milestone 5 uses the public GWIPC snapshot controls in both directions. The
snapshot domain is always `WindowPolicy` for policy state.

## Input snapshots

An input snapshot contains exactly one `PolicyContextUpsert` and zero or more
`PolicyWindowUpsert` items. All items carry `SnapshotItem`. The envelope-level
snapshot ID, generation, expected count, actual count, and item placement are
validated by `libgwipc`; `gwm` additionally requires the policy domain and one
context.

With `MultiOutputPolicy` and `ScaleMetadata` negotiated together, the context
is followed by one `PolicyOutputUpsert` for every known output, zero or more
window records, and at most one `PolicyWindowOutputHint` for each included
window. Item order is accepted independently of canonical hash order. The
snapshot must contain at least one output and exactly one enabled primary;
old profiles reject the M13-only records.

Beginning a snapshot saves the prior pending state and replaces pending state
with an empty staging state. Ending a valid snapshot marks the staged raw state
complete but does not evaluate it. A following `PolicyCommit` performs the
evaluation. `SnapshotAbort` restores the saved pending state.

Before the first accepted snapshot and commit, incremental mutations are not
eligible. After bootstrap, a context or window upsert replaces the complete
pending record, and `PolicyWindowRemove` deletes a pending window. A rejected
commit leaves pending input available for a corrective mutation and later
commit.

## Commit atomicity

`PolicyCommit` IDs are nonzero and strictly increase per connection. Producer
generation is nonzero and cannot decrease. The message requires
`AckRequired`; no snapshot may remain active.

Evaluation occurs into scratch policy state. Output records are encoded and
preflighted against bounded queue limits. A successful commit queues the full
output snapshot and its acknowledgement as one ordered batch before the peer
can observe an acknowledgement. If output cannot be completed, the connection
is discarded and its state is reset, so a partial output cannot be treated as
a committed completed snapshot.

A semantic rejection sends no output snapshot. Its acknowledgement carries the
last committed generation, hash, and count. Committed raw and policy state are
unchanged, while pending raw state remains available for correction.

## Output snapshots

An accepted commit produces:

```text
SnapshotBegin snapshot_id=commit_id domain=WindowPolicy
PolicyWindowState SnapshotItem ...
SnapshotEnd
PolicyAcknowledged Reply reply_to=PolicyCommit.sequence
```

The snapshot generation is the accepted producer generation. Counts include
policy window records only; the context contributes to the hash but is not an
output item. Windows are ordered as visible ascending stack followed by hidden
ascending ID.

M13 output and hint records are input-only; GWM returns the same policy-window
and optional bindings records as before, with output assignment carried by each
policy-window state.

The acknowledgement reports the incoming commit ID and producer generation,
the applied generation, canonical policy hash, total window count, and result.
The reply is validated against the original commit sequence and ID by
`libgwipc`.

## Replacement, reconnect, and determinism

A later complete snapshot replaces the entire pending raw state. Incremental
records not present in that snapshot do not survive replacement. Aborting a
replacement restores the state that was pending before it began.

Disconnect clears pending and committed raw state, committed policy, snapshot
tracking, and commit correlation. A reconnecting peer must bootstrap from a
complete snapshot. Sending the same valid state in a different item order, or
after reconnect, produces the same output records and policy hash. The
synthetic producer and policy tests exercise these deterministic boundaries;
they do not connect the real X11 server or compositor.

An M13 reconnect must reproduce the exact policy-hash v3 value. A different
hash for the same complete replay is deterministic divergence and terminates
the peer relationship rather than accepting altered placement policy.
