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
- [M11 xterm profile](M11_XTERM.md) states the narrow accepted claim and its
  presence-normalization boundary.
- [M12 SDL source audit](M12_SDL_AUDIT.md) records the pinned SDL 2.32.10
  X11 paths, official program hashes, and implementation boundary.
- [M12 SDL profile](M12_SDL.md) freezes the exact build, commands, supported
  SHM/fallback paths, evidence contract, and explicit exclusions.

Milestone 9 is complete for only the pinned commands. The Gentoo VM builds the
verified sources and validates reviewed normalized traces, exact frames,
combined-client isolation, restart behavior, and archive integrity.

Milestone 11 is accepted only for xterm patch 410 under the pinned core-font
ASCII build and invocation. Two full Gentoo VM bootstrap captures selected the
presence-normalized fixture, and the clean full exact run at
`53ec4879b858b96a9b7e8734fb173d037cbc683b` reproduced it with every evidence
gate passing. The accepted run validates interaction, selection, VT and
compositor-restart recovery, canonical frames, graphical-console parity,
restoration, and archive integrity. The passive-grab scope is `GrabButton`
only; `UngrabButton` and passive key grabs are unsupported.

Milestone 12 is not yet an accepted compatibility result. The source,
signature, build profile, repository probes, and official SDL 2.32.10
`testdraw2`/`testsprite2` workloads are pinned, but acceptance remains pending
until the clean M11-to-M12 VM sequence produces a passing M12 summary with no
evidence errors and a valid checksum-protected archive. Do not generalize the
pending profile to another SDL build, renderer, or game.
