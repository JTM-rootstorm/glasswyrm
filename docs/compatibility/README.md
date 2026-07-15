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
  source/build paths and explicitly pending live evidence.
- [M11 xterm profile](M11_XTERM.md) states the narrow intended claim and its
  current unvalidated status.

Milestone 9 is complete for only the pinned commands. The Gentoo VM builds the
verified sources and validates reviewed normalized traces, exact frames,
combined-client isolation, restart behavior, and archive integrity.
