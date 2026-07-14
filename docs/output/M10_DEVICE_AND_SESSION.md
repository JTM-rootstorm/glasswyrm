# Milestone 10 Device and Session Ownership

## Direct test session

The direct path is deliberately narrow and intended for the fixed VM scenario
or controlled spare-VT testing:

```text
--drm-device /dev/dri/cardN --tty /dev/ttyN
```

`gwcomp` opens one verified primary node, owns the resulting descriptor,
activates the exact Linux virtual terminal, enters `VT_PROCESS` and
`KD_GRAPHICS`, and acquires DRM master. Device paths are canonicalized and
checked as character devices; tty paths must be exactly `/dev/tty1` through
`/dev/tty63` and must have matching Linux tty major/minor identity.

The process needs permission to open the selected primary node and tty, switch
VTs, set KD/VT modes, and become DRM master. M10 does not install a setuid
helper, widen device permissions, or add a privileged daemon. The fixed VM
harness runs the direct path through explicit, predeclared commands.

## Externally inherited session

The external path is:

```text
--drm-fd N --external-session
```

The inherited FD is validated as a usable DRM primary node and duplicated with
close-on-exec ownership inside `gwcomp`; the caller's descriptor is never
closed. Capability negotiation and KMS programming occur through the duplicate,
so the external owner must treat the supplied DRM file description as delegated
for the session lifetime. `gwcomp` never acquires or drops DRM master, activates
a VT, changes KD/VT mode, or handles VT release/acquire for this session. The
external owner must provide an already usable display FD and retain all session
lifecycle responsibilities.

This is the future integration seam for a broker such as logind or seatd. M10
does not link either library and does not prescribe their protocol.

## Ownership table

| Operation | Direct session | External session |
|---|---|---|
| Open/duplicate KMS FD | `gwcomp` opens | `gwcomp` duplicates caller FD |
| Acquire/drop DRM master | `gwcomp` | external owner |
| Select and program KMS route | `gwcomp` | `gwcomp` |
| Activate VT | `gwcomp` | external owner |
| Enter/restore KD and VT modes | `gwcomp` | external owner |
| Handle VT process signals | `gwcomp` | external owner |
| Restore captured KMS state | `gwcomp` | `gwcomp` |
| Close caller's inherited FD | not applicable | external owner |

In both modes, `Device` is the sole owner of its internal FD and KMS helpers
only borrow it. Page-flip cookies remain live until the event is serviced or
the pending presentation is explicitly aborted.

## Security boundary

Glasswyrm remains local-only. The session code adds no TCP listener, setuid
binary, hidden thread, or general command-execution surface. It does not claim
Wayland-like client isolation. An administrator must provision access to the
specific device and tty; the program does not silently choose among ambiguous
devices or connectors.
