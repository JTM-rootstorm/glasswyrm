# Milestone 13 GW_SCALE Protocol

`GW_SCALE` version 0.1 is Glasswyrm's bounded prototype for rational output
scale discovery and integer client-buffer-scale publication. It is available
only when the scale-protocol profile is enabled. It is independent of the
Milestone 12 game-compatibility profile and does not claim toolkit integration.

## Registry

| Name | Major opcode | First event | Events | First error | Errors | Version |
|---|---:|---:|---:|---:|---:|---:|
| GW_SCALE | 135 | 69 | 1 | 139 | 2 | 0.1 |

The extension defines `BadScale` at absolute error code 139 and `BadScaleMode`
at absolute error code 140. Its request minor opcodes are:

| Minor | Request |
|---:|---|
| 0 | QueryVersion |
| 1 | SelectInput |
| 2 | GetOutputScale |
| 3 | GetWindowScale |
| 4 | SetWindowBufferScale |
| 5 | PresentScaledPixmap |
| 6 | ResetWindowBufferScale |

All unused fields and padding bytes must be zero. Requests with a length other
than the exact length documented below receive `BadLength`. Multi-byte values
use the requesting client's byte order. A 64-bit value is encoded as its high
`CARD32` followed by its low `CARD32`; each word uses client byte order.

## Event selection

`SelectInput` records a mask per client and direct-root InputOutput window:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | PreferredScale | Preferred scale or primary output changed |
| 1 | OutputMembership | Output membership changed |
| 2 | BufferScaleInvalidated | An active scaled presentation became invalid |

Unknown mask bits are `BadValue`. Selection is removed when the client
disconnects or the selected window is destroyed.

## Requests and replies

### QueryVersion

The 12-byte request contains the standard extension request header followed by
client major and minor versions as `CARD32`. The 32-byte reply contains server
major and minor version 0 and 1. Negotiation never reports a newer version.

### SelectInput

The 12-byte request contains a `WINDOW` and `CARD32` event mask. It has no
reply. The window must be a direct child of the root, must be InputOutput, and
must be owned by the requesting client.

### GetOutputScale

The 8-byte request contains a RANDR output XID. Unknown or stale output handles
receive core `BadValue`; this keeps `GW_SCALE` usable independently of RANDR's
profile switch.

The fixed 60-byte reply has a reply length of 7 and contains, in order:

```text
CARD32 output_id_high
CARD32 output_id_low
INT32  logical_x
INT32  logical_y
CARD32 logical_width
CARD32 logical_height
CARD32 physical_width
CARD32 physical_height
CARD32 scale_numerator
CARD32 scale_denominator
CARD16 transform
CARD8  primary
CARD8  enabled
CARD32 layout_generation_high
CARD32 layout_generation_low
```

The identifier and generation are the internal stable 64-bit values. Boolean
fields are canonical zero or one. Transform values use the GWIPC output
transform registry.

### GetWindowScale

The 8-byte request contains a direct-root InputOutput `WINDOW`. The reply is 40
bytes plus four bytes per current RANDR output membership and has reply length
`2 + membership_count`:

```text
WINDOW window
OUTPUT primary_output
CARD32 preferred_scale_numerator
CARD32 preferred_scale_denominator
CARD32 accepted_buffer_scale
CARD16 scale_mode
CARD16 membership_count
CARD32 layout_generation_high
CARD32 layout_generation_low
OUTPUT memberships[membership_count]
```

Memberships use deterministic layout order. An entirely offscreen window has
an empty list and output `None`; its preferred scale remains the server's
documented fallback.

### SetWindowBufferScale

The 12-byte request contains a direct-root InputOutput `WINDOW` and a requested
integer buffer scale. Values 1 through 4 are accepted; zero, fractional values,
and larger values receive `BadScale`. The window must be owned by the client.

The 32-byte reply reports the accepted integer scale, current preferred scale
numerator and denominator, and current 64-bit layout generation. Logical window
geometry does not change. Success enters the scale-aware-awaiting-pixmap state,
while the current legacy backing remains visible.

### PresentScaledPixmap

The 20-byte request contains:

```text
WINDOW window
PIXMAP pixmap
XFIXESREGION damage_or_none
CARD32 presentation_serial
```

The pixmap must be depth-24 XRGB shared storage whose dimensions equal the
logical window dimensions multiplied by the accepted integer buffer scale. The
window must be a client-owned direct-root InputOutput window with an accepted
scale and no mapped InputOutput child. A non-None damage region must exist and
all of its rectangles must be bounded by the pixmap. Violating the scale-mode
or child-window requirements receives `BadScaleMode`; an incompatible scale or
size receives `BadScale`; ordinary resource/type failures use their core or
XFixes errors.

The 32-byte reply echoes the accepted presentation serial and reports the
accepted buffer scale and current 64-bit layout generation. Success retains the
pixmap's shared storage independently of its XID and publishes it without
changing logical geometry.

### ResetWindowBufferScale

The 8-byte request contains a direct-root InputOutput `WINDOW` and has no
reply. It discards the current scaled-pixmap source, returns the window to
legacy window/subtree backing, and restores client buffer scale 1.

## ScaleNotify

Event code 69 is a fixed 32-byte event:

```text
CARD8  response_type
CARD8  reason_mask
CARD16 sequence
WINDOW window
OUTPUT primary_output
CARD32 preferred_scale_numerator
CARD32 preferred_scale_denominator
CARD32 accepted_buffer_scale
CARD32 layout_generation_high
CARD32 layout_generation_low
```

The reason mask uses the same three bits as `SelectInput`. Notifications are
generated only after the relevant output/window state commits. A notification
may combine reasons. Output movement that preserves membership and preferred
scale does not by itself invalidate the active buffer.

## Window state machine

Each eligible window is in one of these states:

- `Legacy`: window/subtree backing, client buffer scale 1;
- `ScaleAwareAwaitingPixmap`: an integer scale is accepted but legacy backing
  remains visible;
- `ScaleAwareActive`: retained scaled-pixmap storage is the presentation source.

`SetWindowBufferScale` moves Legacy or Active to Awaiting. A valid
`PresentScaledPixmap` moves Awaiting or Active to Active and atomically replaces
the retained source. `ResetWindowBufferScale` returns either scale-aware state
to Legacy. Resizing the window or mapping an InputOutput child moves Active to
Awaiting, preserves the accepted integer scale, invalidates the incompatible
source, and emits the selected invalidation notification. Moving a window or
committing a new output scale does not discard dimension-compatible storage.

Freeing the source pixmap XID does not invalidate an already retained frame.
Future updates nevertheless require a live pixmap XID and a new
`PresentScaledPixmap` request.

## Damage and publication

Buffer damage and surface-logical damage are distinct. For a scaled pixmap,
buffer rectangles are clipped in pixmap pixels, copied into the published
memfd, and converted to logical surface damage by flooring lower bounds and
ceiling upper bounds after division by the integer client scale. The published
surface retains its logical dimensions; its attached buffer carries the scaled
pixel dimensions and client-scale metadata. Eventfd readiness remains one
signal per buffer/frame.

The compositor then applies the rational output scale and output transform.
Fractional or downscaled sampling uses the renderer's fixed bilinear contract,
including one-pixel native-output damage expansion. Direct and nearest-neighbor
paths do not add filter padding.

## Validation order and robustness

Decoders first validate exact length and zero padding, then resource existence
and type, ownership and direct-root eligibility, scale-mode state, pixmap
format/dimensions, child-window restrictions, and damage bounds. Failed
requests do not partially mutate window state or publication state.

All per-client event selections and retained presentation references are
bounded by existing resource limits and cleaned up on disconnect. The protocol
does not add a listener, shared-memory object, or privilege boundary of its own.
It remains part of Glasswyrm's documented local same-UID security model.

## Compatibility boundary

`GW_SCALE` is proven by Glasswyrm's repository raw-wire and XCB client only.
Legacy clients continue to publish scale 1 buffers and require no protocol
changes. No claim is made that SDL, Xlib, XCB, or existing toolkits natively
negotiate this extension.
