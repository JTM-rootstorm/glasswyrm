# Compatibility profiles

Compatibility claims in this directory are versioned, command-specific, and
evidence-driven. A request appearing in server code does not by itself prove
that an external application works.

- [Milestone 9 client audit](M9_CLIENT_AUDIT.md) records source-derived scope
  and the reviewed observed traffic.
- [xeyes profile](M9_XEYES.md) records the one targeted xeyes invocation.
- [xclock profiles](M9_XCLOCK.md) record the analog and digital invocations.
- [M9 request profile](M9_REQUEST_PROFILE.md) summarizes implemented protocol
  scope and acceptance status.
- [M11 xterm source audit](M11_XTERM_AUDIT.md) records the verified patch 410
  source/build paths, deterministic presence-normalized A/B bootstrap trace,
  and evidence boundary.
- [M11 xterm profile](M11_XTERM.md) states the narrow candidate claim and its
  remaining exact-fixture gate.

Milestone 9 is complete for only the pinned commands. The Gentoo VM builds the
verified sources and validates reviewed normalized traces, exact frames,
combined-client isolation, restart behavior, and archive integrity.

Milestone 11 bootstrap evidence is complete only for xterm patch 410 under the
pinned core-font ASCII build and invocation. Two full Gentoo VM captures at the
same commit reproduced the presence-normalized trace and validated interaction,
selection, VT and compositor-restart recovery, canonical frames,
graphical-console parity, restoration, and archive integrity. Final acceptance
remains pending until the post-fixture full VM run passes exact comparison.
The passive-grab scope is `GrabButton` only; `UngrabButton` and passive key
grabs are unsupported.
