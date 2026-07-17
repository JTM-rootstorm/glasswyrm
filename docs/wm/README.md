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
- [M6 Lifecycle Policy](M6_LIFECYCLE_POLICY.md)
- [M11 Interactive Policy](M11_INTERACTIVE_POLICY.md)

M6 adds an explicit `glasswyrmd` lifecycle peer while preserving the M5
synthetic regression boundary. Input, decoration rendering, multiple
workspaces remain deferred. Milestone 11 capability-gates one interactive
binding record without changing legacy policy snapshots.
