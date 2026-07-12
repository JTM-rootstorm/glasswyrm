# Glasswyrm Window Policy

Milestone 5 replaces the `gwm` placeholder with an independently executable,
headless policy service. A repository-owned synthetic ProtocolServer sends raw
top-level window state through installed GWIPC 0.3 APIs. `gwm` evaluates a
single-workspace policy and returns complete policy snapshots.

The implementation is split between:

- `src/wm/`: pure deterministic state, validation, transactions, hashing, and
  policy evaluation;
- `src/gwm/`: command-line handling, GWIPC listener, bounded reactor, message
  dispatch, acknowledgements, and peer lifecycle;
- `tests/integration/gwm_m5_producer.cpp`: public-API synthetic policy producer;
- `tests/wm/`: focused policy and transaction tests.

Documents:

- [M5 Policy Service](M5_POLICY_SERVICE.md)
- [Policy Algorithm](POLICY_ALGORITHM.md)
- [Policy Snapshots](POLICY_SNAPSHOTS.md)
- [Decision 0007](../decisions/0007-window-manager-policy-scaffold.md)

This directory documents only the synthetic M5 boundary. `glasswyrmd` and
`gwcomp` do not connect to `gwm`; X11 mapping, input, decoration rendering,
multiple workspaces, and real display integration remain deferred.
