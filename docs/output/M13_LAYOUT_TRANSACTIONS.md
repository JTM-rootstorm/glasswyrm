# Milestone 13 layout transactions

Output changes are complete-layout, generation-guarded transactions. A client
cannot mutate one field in isolation or create an output outside compositor
inventory. `gwout` first queries the current snapshot, edits one known output,
derives exact logical dimensions, validates the complete proposal, and commits
against the queried generation.

## Staging order

`glasswyrmd` permits one active output transaction. It copies and validates the
requested layout, captures the previous output, window, pointer, focus,
membership, RANDR, and EWMH state, and marks output configuration busy.

The server then:

1. sends the proposed output map and window hints to `gwm`;
2. validates the returned window state and output assignments;
3. constructs one complete compositor snapshot containing every output,
   surface, policy record, membership, buffer, cursor, and affected damage;
4. requests one atomic frame-set presentation; and
5. promotes server-visible state only after compositor acceptance.

Promotion updates the layout generation, window state, dynamic root geometry,
RANDR topology and timestamps, EWMH root properties, input bounds, pointer and
cursor state, then queues applicable X11, RANDR, and `GW_SCALE` events. The
tool acknowledgement is last. An existing X11 client remains connected, and a
window's logical geometry changes only when policy requires it.

## Rejection and rollback

Pure validation failures change no peer or server state. If the compositor
rejects after GWM accepted the proposal, the server replays the previous
policy snapshot, requires its previous v3 hash, and restores the previous
compositor scene when needed. Server state and client events remain unchanged.
Rollback failure is fatal because continuing would leave process authorities
in disagreement.

Lifecycle mutations are held behind the existing transaction barrier while a
configuration is active. Safe queries and canonical drawing may continue;
drawing damage is accumulated. Interactive move or resize starts pause, and a
second commit receives `Busy`. Disconnecting the submitting tool does not
corrupt an already submitted transaction.

Accepted layouts may change headless enablement, position, scale, transform,
and primary output. The M13 DRM profile accepts only compositor scale and
software transform changes for its fixed mode and origin. Physical mode
setting through `gwout` or RANDR is unsupported.
