# X11 Milestone 1 Compatibility

Milestone 1 implements only the initial local X11 connection setup exchange.
Compatibility claims are intentionally limited to behavior covered by the
protocol and integration tests.

## Supported

- Local filesystem Unix socket
- X11 11.0 setup
- Little- and big-endian setup
- Zero-length authorization
- Deterministic synthetic one-screen setup reply
- Raw toy setup clients
- XCB connect/setup/disconnect probe

## Unsupported

- TCP
- Abstract Unix socket guarantee
- Xauthority and `MIT-MAGIC-COOKIE-1`
- Normal X11 requests
- Errors, replies, or events after setup
- Resources, windows, atoms, or properties
- Input
- Window-manager or compositor IPC
- Rendering and display output

Supplying authorization data causes setup to fail; it is never silently
ignored. Allowing an unauthenticated local setup request is a Milestone 1
research limitation and is unsafe for a real multi-user desktop.

The XCB result means only that a libxcb client can complete connection setup,
inspect the setup record, and disconnect. It does not imply that normal XCB or
X11 applications work.
