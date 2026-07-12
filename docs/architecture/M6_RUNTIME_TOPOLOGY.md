# Milestone 6 Runtime Topology

## Processes

```text
X11 clients -> glasswyrmd -> gwm
                         -> gwcomp
```

`gwm` listens as `WindowManager`; `gwcomp` listens as `Compositor`;
`glasswyrmd` connects to both as `ProtocolServer`. The peers do not communicate
directly and share no in-process state.

## Startup

Integrated mode requires both `--wm-socket` and `--compositor-socket`.
`glasswyrmd` creates signal infrastructure, connects and bootstraps `gwm`, then
connects and bootstraps `gwcomp`. Only after both acknowledgements validate does
it create the X11 display socket. Attempts use monotonic 50, 100, 200, 400, 800,
and 1000 millisecond backoff with a ten-second bootstrap deadline.

The fixed M6 model is root 1, workspace 1, output 1, 1024x768, scale 1/1,
normal transform, sRGB, and 60000 millihertz. Dimensions originate in the
server `ScreenModel`.

## Roles and capabilities

| Connection | Required capabilities |
| --- | --- |
| `glasswyrmd` to `gwm` | Snapshots, WindowPolicy, WindowLifecycle |
| `glasswyrmd` to `gwcomp` | Snapshots, OutputState, SurfaceState, SdrColorMetadata, FrameAcknowledgement, WindowLifecycle |

Lifecycle connections send no descriptors. `gwcomp` separately retains its M4
`TestProducer` buffer path.

## Reactor and transaction states

All processes are single-threaded and use `poll()`. Integrated server states
cover peer bootstrap, idle, policy acknowledgement, compositor
acknowledgement, rollback, peer wait, and fatal divergence. One operation is
active and later operations use a bounded FIFO. A deferred client is blocked
without consuming its next request twice.

The X11 listener stays absent before initial synchronization. After startup it
remains owned during dependency loss, while new accepts are paused until replay
completes. The current tree has unit coverage for coordinator replay and
rollback state transitions. The fixed M6 restart-hold probe also proves
end-to-end same-client survival across real peer restarts.

## Failure policy

Malformed messages and semantic mismatches are rejected at their owning
boundary. A compositor rejection after accepted policy initiates a committed
policy and compositor rollback. Failure to prove rollback or expiry of the
transaction deadline is fatal; the server does not continue with divergent
authority.
