# Milestone 13 output identities

Output identity is stable backend metadata, not a transient protocol handle.
The internal 64-bit IDs are authoritative across policy and compositor
restart. Startup fails on a collision or when an output-model backend cannot
construct a stable identity.

## Headless outputs

A headless output ID is the repository FNV-1a 64-bit hash of:

```text
glasswyrm:headless:<output-name>
```

The implementation applies the nonzero output namespace bit and checks for
collisions. Reordering a layout, disabling an output, or restarting a peer does
not change the ID. Names are unique bounded ASCII identifiers supplied by
`--headless-output`.

## DRM output

The single M13 DRM output identity is derived from stable available facts in
this order:

1. device bus or sysfs identity;
2. connector type, index, and name;
3. EDID digest when available.

Inventory records whether EDID participated. Connector object IDs are not a
fallback because they are transient. If stable facts are unavailable, the
output-model profile fails rather than publishing an unstable ID.

## Modes and RANDR handles

A mode ID is derived from its output ID, pixel dimensions, refresh in
millihertz, flags, and mode name. Modes therefore remain associated with their
own output even when two outputs advertise identical dimensions and refresh.

RANDR output, CRTC, and mode XIDs are server-owned 32-bit protocol handles.
Assignment is deterministic from internal identities, and a live handle is
never reused for a different object. Layout changes and compositor reconnect
preserve mappings, but clients must not treat a RANDR XID as the persistent
output identity. `GW_SCALE`, `gwinfo`, and `gwout` expose the internal 64-bit
ID where persistent correlation is needed.

M13 does not persist identities across independent server sessions in a
configuration database; output persistence remains unsupported.
