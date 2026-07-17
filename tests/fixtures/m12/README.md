# Milestone 12 compatibility fixtures

`result-contract.json` freezes the repository-owned raw, XCB, and public-SDL
probe matrix. `SHA256SUMS` protects that contract. The VM harness validates the
contract before executing either the MIT-SHM or MIT-SHM-disabled profile.

Canonical software/GLES frames and DRM screenshots are runtime evidence, not
manufactured repository fixtures. The clean VM acceptance archives them with
its own `SHA256SUMS` and requires byte equality between canonical output and
the graphical-console capture. An accepted runtime frame may be promoted here
only with its exact workload, tested commit, and source provenance recorded.
