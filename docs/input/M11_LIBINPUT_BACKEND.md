# Milestone 11 libinput Backend

The real-input profile is built with `-Dlibinput_backend=true`. The option is
off by default and, when off, does not discover or link libinput or
libxkbcommon. Enabling it requires Linux, `glasswyrmd`, `libgwipc`, libinput,
libxkbcommon, xkeyboard-config data, and timerfd support.

Each repeated `--libinput-device PATH` names a device for libinput's path
backend. Paths are canonicalized and recorded by device and inode identity;
the restricted-open callback refuses paths outside that startup allowlist.
At least one keyboard-capable and one pointer-capable device are required.
There is no udev enumeration, hotplug search, seat selection, touch, tablet,
gesture, or multi-seat policy.

The libinput FD participates in the server poll reactor. A dispatch drains at
most 256 events/work units, destroys every provider event, and emits bounded
internal motion, button, or key records. Relative deltas accumulate before
integer pointer movement; absolute positions are transformed into root
coordinates. Linux buttons map to core buttons 1-3, and vertical/horizontal
v120 wheel steps map to buttons 4/5 and 6/7. Timestamps are converted from
microseconds to nonzero monotonic 32-bit milliseconds.

The compositor's negotiated session-state request suspends input before VT
release and resumes it after display reacquisition. Suspension cancels repeat
and clears held provider/keymap state without synthetic releases. Resume
revalidates keyboard and pointer availability and resets fractional motion.
A failed resume is fatal to the real-input profile; removal of the last device
for a required capability marks input unavailable without disconnecting X11
clients.

Real devices and `--synthetic-input-socket` are mutually exclusive. The M8
synthetic profile remains a separately selected deterministic path.
