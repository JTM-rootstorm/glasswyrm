# Integration Tests

This directory contains process-boundary tests for the X11 service, GWIPC, and
the headless compositor. Milestone 4 coverage starts `gwcomp` with private
temporary socket and dump directories and drives it with the repository-owned
`gwcomp_m4_producer` through installed/public `libgwipc` APIs.

`gwcomp_process_test.cpp` covers CLI and listener lifecycle behavior.
`gwcomp_golden_test.cpp` covers an accepted synthetic frame and exact pixels.
Scenario-level rejection, release, reconnect, malformed-peer, work-budget, and
leak regressions belong here as their producer cases are completed.
