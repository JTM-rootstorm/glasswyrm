# X11 Milestone 2 Compatibility

Milestone 2 extends the local X11 11.0 setup service with a deliberately small
headless core request profile. Compatibility claims are limited to repository
tests and fixed raw/libxcb probes.

## Supported transport and setup

- Local filesystem Unix socket
- X11 11.0 setup in little- or big-endian byte order
- Zero-length authorization only
- One deterministic synthetic screen and root window
- Incremental, pipelined ordinary request framing
- Core replies and errors in the client's byte order
- 65,535-unit ordinary request maximum
- 1 MiB per-client queued-output cap

## Supported core requests

| Opcode | Request | Milestone 2 behavior |
|---:|---|---|
| 1 | `CreateWindow` | Creates an unmapped in-memory child window. |
| 4 | `DestroyWindow` | Recursively destroys a non-root window tree. |
| 14 | `GetGeometry` | Returns stored window geometry. |
| 15 | `QueryTree` | Returns root, parent, and ordered children. |
| 16 | `InternAtom` | Resolves predefined and server-global dynamic atoms. |
| 17 | `GetAtomName` | Returns an atom's exact name. |
| 18 | `ChangeProperty` | Replaces, prepends, or appends typed property data. |
| 19 | `DeleteProperty` | Removes a property when present. |
| 20 | `GetProperty` | Returns a typed, sliced property value. |
| 21 | `ListProperties` | Returns property atoms on a window. |
| 43 | `GetInputFocus` | Returns a synthetic synchronization reply only. |
| 127 | `NoOperation` | Advances sequence and produces no reply. |

The state model includes client resource-ID validation, a recursive window
tree, disconnect cleanup, all predefined core atoms, dynamic atoms shared by
clients, and byte-order-independent storage for format-16 and format-32
properties. Unsupported core opcodes return `BadRequest`; recoverable request
errors do not close the connection.

Format-8 property values are stored as bytes. Format-16 and format-32 values
are stored as canonical typed units and encoded in the receiving client's byte
order. A property is limited to 4 MiB, a window to 4,096 properties, and total
server property data to 64 MiB.

The repository-owned proof commands are:

```sh
./build/tests/x11_milestone2_probe --display :99 --byte-order little --basic
./build/tests/x11_milestone2_probe --display :99 --byte-order big --basic
./build/tests/x11_milestone2_probe --display :99 --errors
./build/tests/x11_milestone2_probe --display :99 --cleanup
./build/tests/x11_milestone2_probe --display :99 --cross-endian
DISPLAY=:99 XAUTHORITY=/dev/null ./build/tests/xcb_milestone2_probe
./tools/gw-vm milestone2-runtime-test --yes
```

## Unsupported

- Non-empty authorization, Xauthority, or TCP
- Mapping, unmapping, configuring, reparenting, or stacking policy
- Events, event delivery, input, grabs, or real focus
- Window-manager or compositor IPC
- Rendering, buffers, framebuffers, display output, or DRM/KMS
- Pixmap, graphics-context, font, cursor, or colormap creation
- Selections and clipboard behavior
- X11 extensions, including BIG-REQUESTS
- Normal toolkit or application compatibility

A libxcb probe can create an unmapped window resource, manipulate atoms and
properties, query server state, destroy the resource, and disconnect. This does
not mean xclock, xeyes, xterm, GTK, Qt, SDL, browsers, or games work.
