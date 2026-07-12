# Compositor Primitives

This directory contains the hardware-independent scene primitives used by
`gwcomp`: checked rectangle operations, bounded damage accumulation, and a
staged/committed scene model. The model supports one output, bounded surface
state, complete-snapshot bootstrap, deterministic stacking, and atomic scene
commits. Read-only, bounded memfd buffer import also lives here.

These primitives are covered by unit tests, but they are not yet wired into the
`gwcomp` process reactor. The current reactor owns a real GWIPC listener and
negotiates one `TestProducer`; it rejects frame commits as incomplete. Buffer
attachment dispatch, rendering a committed scene, frame dumps from the process,
and a repository-owned synthetic producer remain to be integrated.

See [`docs/compositor/`](../../docs/compositor/) for the Milestone 4 contract,
pixel rules, frame-dump format, and current implementation boundary.
