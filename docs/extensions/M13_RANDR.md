# Milestone 13 RANDR Multi-Output Profile

Milestone 13 adds a bounded multi-output reporting profile to the historical
Milestone 12 RANDR 1.3 subset. It remains a read-mostly compatibility surface:
`gwout`, not RANDR, owns output configuration requests.

## Object identity

Glasswyrm assigns server-owned 32-bit RANDR XIDs to the compositor inventory's
stable 64-bit output and mode IDs. Assignment starts from the historical
`0x100` output, `0x101` CRTC, and `0x102` mode handles, proceeds in stable
internal-ID order, and never reuses a live handle for a different object.
Layout reordering, disabling an output, and compositor reconnect do not change
an existing mapping.

All output descriptors remain visible in `GetScreenResources` and
`GetScreenResourcesCurrent`. An enabled output contributes one active CRTC;
a disabled output remains listed but has no current CRTC. Descriptor modes and
the current bounded arbitrary headless mode are exposed as stable mode objects.
The output-layout generation is the RANDR configuration timestamp source.

## Reporting

The M13 profile generalizes these existing requests to the current bounded
layout:

- `GetScreenInfo` and `GetScreenSizeRange`;
- `GetScreenResources` and `GetScreenResourcesCurrent`;
- `GetOutputInfo`;
- `GetCrtcInfo` and `GetCrtcGammaSize`;
- `GetOutputPrimary`;
- the empty output-property query subset; and
- `SelectInput`.

Screen resources contain every current output and mode in deterministic order,
and active CRTCs in output-layout order. CRTC positions and extents are global
logical X11 coordinates. Mode dimensions remain physical pixels. The root
screen extent is the complete logical layout bounding box.

Glasswyrm output transforms map to RANDR rotation fields as follows:

| Glasswyrm transform | RANDR rotation mask |
|---|---:|
| Normal | Rotate0 |
| Rotate90 | Rotate90 |
| Rotate180 | Rotate180 |
| Rotate270 | Rotate270 |
| Flipped | Rotate0 + ReflectY |
| Flipped90 | Rotate90 + ReflectY |
| Flipped180 | Rotate180 + ReflectY |
| Flipped270 | Rotate270 + ReflectY |

`ReflectY` represents Glasswyrm's horizontal, left-to-right reflection. X11
logical coordinates themselves do not rotate.

## Configuration boundary

`SetCrtcConfig` succeeds only when the request exactly restates the selected
CRTC's current mode, logical position, rotation, and single attached output.
Every attempted mutation returns RANDR `Failed` without changing server or KMS
state. Output enablement, mode, position, rational scale, transform, and primary
selection are changed atomically through the same-UID `gwout` control protocol.

After an accepted control transaction, selected clients receive one coherent
configuration generation of `ScreenChangeNotify`, CRTC-change `Notify`, and
output-change `Notify` records for the state that changed. Failed or rolled-back
transactions emit no RANDR events.

## Compatibility boundary

The advertised maximum remains RANDR 1.3. This profile proves only the
repository raw-wire/XCB probes and the pinned SDL two-display regression. It
does not add RANDR-driven mode setting, providers, monitor objects, leases,
gamma control, panning, hotplug, clone configuration, or DRM/KMS routing.

Without the Milestone 13 output-model profile, every historical Milestone 12
reply, object handle, event, and idempotent configuration result remains exact.
