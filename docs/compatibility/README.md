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

Milestone 9 is complete for only the pinned commands. The Gentoo VM builds the
verified sources and validates reviewed normalized traces, exact frames,
combined-client isolation, restart behavior, and archive integrity.
