# Architecture Notes

This directory is for architecture notes that expand on
`docs/GLASSWYRM_SPEC.md`.

Milestone 0 establishes three distinct executable targets without implementing
their runtime responsibilities:

| Process | Future authority | First behavioral milestone |
|---|---|---|
| `glasswyrmd` | X11 protocol truth | Milestone 1 |
| `gwm` | Window-management policy truth | Milestone 4 |
| `gwcomp` | Composition and display authority | Milestone 3 |

The placeholders share only a tiny status-printing helper. They do not define a
runtime API or IPC contract, and they do not communicate. `libgwipc`, headless
composition, software rendering, and synthetic surface import are deliberately
deferred to Milestone 3.

Future behavior must keep these processes independently testable and preserve
the authority boundaries in the project specification.
