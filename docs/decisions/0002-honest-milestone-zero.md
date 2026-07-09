# 0002: Keep Milestone 0 as an Honest Skeleton

Status: Accepted

## Context

The first repository skeleton included small protocol, input, scene, headless
backend, and software-framebuffer models. The updated specification assigns
those behaviors to Milestones 1 and 3 and defines a three-process runtime split.
Keeping speculative models in Milestone 0 would imply contracts and support
before either had been designed or tested at its proper milestone.

## Decision

Milestone 0 builds separate placeholders for `glasswyrmd`, `gwm`, and `gwcomp`,
plus the named developer tools. A small internal helper gives the placeholders
consistent identity and status output; it is not an installed public library or
a runtime IPC contract.

Meson exposes independent `glasswyrmd`, `gwm`, `gwcomp`, and `tools` switches so
the process/package split is visible from the beginning. The future-facing
backend, renderer, assembly, IPC tracing, built-in policy, and experimental
switches named by the specification are accepted as reserved configuration,
but they do not imply implemented behavior. Sanitizer options remain active
because they validate the build itself.

The Milestone 0 test harness contains one scaffold identity test. Protocol,
window-manager policy, IPC, integration, and pixel tests remain reserved for the
milestones that implement those behaviors.

## Consequences

- The repository configures, builds all named placeholders, and runs a test.
- Runtime authority boundaries are visible without inventing an IPC ABI.
- No X11, compositor, renderer, input, or hardware behavior is claimed.
- Future milestones must replace placeholders with tested behavior rather than
  quietly promoting speculative scaffold code.
