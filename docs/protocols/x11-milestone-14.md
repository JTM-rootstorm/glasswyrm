# X11 compatibility status: Milestone 14

Milestone 14 adds the experimental `GW_VRR` 0.1 extension and does not widen
the accepted core X11, ICCCM, EWMH, RANDR, XFixes, Damage, Render, Composite,
MIT-SHM, or BIG-REQUESTS profiles.

## Added behavior

- `QueryExtension` advertises `GW_VRR` only with the explicit VRR protocol
  profile.
- QueryVersion negotiates version 0.1.
- Clients can select VRR notifications on owned top-level windows.
- Clients can read and transactionally set Default, Disable, Allow, or Prefer.
- Window queries expose committed policy, candidate, effective, reasons, and
  generations.
- Output queries expose committed capability, policy, candidate, effective,
  range, timing, and reasons through RANDR output XIDs.
- Notifications are delivered after accepted lifecycle or output commits.
- Raw little- and big-endian clients receive exact replies, events, and
  extension errors.

## Compatibility boundary

Application preference is advisory. It does not grant a client KMS access,
force enablement, change pixels, pace frames, or imply fullscreen. GWM still
owns window suitability and `gwcomp` still owns surface validation and final
display state.

The pinned xeyes, xclock, xterm, SDL probe, SDL `testdraw2`, SDL `testsprite2`,
and M13 legacy/scale profiles remain unchanged. SDL fullscreen-desktop and
borderless windows may qualify under output policy Fullscreen, but SDL is not
treated as AppRequested unless it explicitly uses `GW_VRR` Prefer.

No broad toolkit compatibility claim is made. PRESENT timing, DRI3, client
DMA-BUF, direct scanout, tearing, and persistent per-application profiles are
not implemented.
