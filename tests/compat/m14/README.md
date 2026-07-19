# Milestone 14 compatibility scaffolding

`validate_client_state.py` validates only the bounded v2 state emitted by the
repository-owned `m14_vrr_client`, including event selection, preference
replies/notifications, known reason bits, and eventfd cadence synchronization.
It explicitly does not turn client state into hardware proof.

`validate_host_fixtures.py` checks the checksum-protected deterministic policy,
protocol, tool, headless, fake-DRM, and QXL-negative fixtures after the G2/G3
freeze. `validate_vm_evidence.py` separately validates a complete QXL VM run.
Positive physical evidence remains external to these host fixtures and must be
produced only by the reviewed hardware harness.
