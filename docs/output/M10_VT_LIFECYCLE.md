# Milestone 10 Virtual-Terminal Lifecycle

This lifecycle applies only to direct DRM sessions. An externally inherited
session owns no VT behavior inside `gwcomp`.

## Acquisition

For an exact `/dev/ttyN` path, `gwcomp`:

1. opens with `O_RDWR|O_CLOEXEC|O_NOCTTY|O_NOFOLLOW`;
2. verifies the matching Linux tty character-device identity;
3. saves `VT_GETSTATE`, `VT_GETMODE`, and `KDGETMODE` results;
4. activates the requested VT with `VT_ACTIVATE` and `VT_WAITACTIVE`;
5. enters `VT_PROCESS` with distinct release/acquire signals;
6. enters `KD_GRAPHICS`; and
7. acquires DRM master.

M10 uses `SIGUSR1` for release and `SIGUSR2` for acquire. Signal handlers write
tagged bytes to the compositor's nonblocking self-pipe; they perform no DRM,
VT, allocation, logging, or compositor work.

## Release and acquire

When SessionState is negotiated for an M11 real-input session, release first
quiesces presentation and requests Inactive from `glasswyrmd`. DRM master is
dropped and the VT release acknowledged only after the correlated reply
confirms that libinput is suspended. Transactions already queued ahead of that
reply are drained through the normal validator under the existing per-turn
bounds; later producer processing remains stopped. Acquire acknowledges the VT,
reacquires DRM master, restores the committed frame by complete modeset, then
requests Active. Producer processing resumes only after real input acknowledges
a successful resume. Timeout, rejection, or malformed correlation is fatal and
enters restoration.

When SessionState is not negotiated, the exact M10 sequence below remains in
effect.

On a release notification, the runtime stops producer consumption, waits for
the pending presentation boundary to quiesce, drops DRM master, acknowledges
the release with `VT_RELDISP`, and marks presentation suspended.

On acquire, it acknowledges with `VT_RELDISP(VT_ACKACQ)`, reacquires DRM
master, copies the last committed software frame into a scanout buffer, verifies
its hash, and performs a complete modeset. Producer consumption resumes only
after that committed frame is visible again. VT transitions are reportable
evidence, not producer frames, and do not increment the producer frame count.

Failure to quiesce, drop/reacquire master, acknowledge a VT transition, or
re-modeset the committed frame is fatal.

## Ordered shutdown

Normal shutdown and handled fatal paths attempt this exact order:

1. stop consuming producer contracts and clear any uncommitted evidence;
2. regain the Glasswyrm VT and DRM master if shutdown begins suspended;
3. restore and read back the saved KMS connector/CRTC/plane state;
4. remove framebuffer IDs, unmap memory, and destroy dumb buffers;
5. drop DRM master;
6. restore the saved KD mode;
7. restore the saved VT mode;
8. reactivate and wait for the previously active VT; and
9. close the tty and DRM descriptors.

Each restoration dimension is recorded separately. A failure produces a
nonzero process result; later safe restoration steps are still attempted. If
KMS restoration itself fails, active scanout resources are retained rather
than being removed underneath the hardware.

The `VT_GETSTATE` open-VT bitmask is retained in before-and-after evidence for
diagnostics, but it is not a restoration invariant: unrelated gettys or test
clients can open or close a VT during the session. Acceptance compares the
active VT and signal together with the complete VT mode and KD mode instead.

## Limitations and recovery

`SIGINT` and `SIGTERM` use the normal runtime shutdown path. `SIGKILL` cannot be
handled and therefore cannot provide restoration guarantees. Kernel failure,
device removal, and power loss have the same fundamental limitation. Use the
recovery steps in [hardware validation](M10_HARDWARE_VALIDATION.md) after a
forced termination.
