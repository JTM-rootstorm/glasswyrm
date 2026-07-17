# Milestone 12 fixture bootstrap

`result-contract.json` freezes the repository-owned raw, XCB, and public-SDL
probe matrix. `SHA256SUMS` protects that contract. The VM harness validates the
contract before executing either the MIT-SHM or MIT-SHM-disabled profile.

Canonical software/GLES frames and DRM screenshots are runtime evidence, not
manufactured repository fixtures. The clean VM acceptance archives them with
its own `SHA256SUMS` and requires byte equality between canonical output and
the graphical-console capture. An accepted runtime frame may be promoted here
only with its exact workload, tested commit, and source provenance recorded.

The complete Section 37 fixture inventory is intentionally absent until one
clean M12 acceptance run passes every host, VM, renderer-equivalence, DRM,
restoration, and archive gate at one committed revision. A failed or partial
runtime artifact must never be copied here.

The acceptance harness retains both byte-order registry results and produces
bounded normalized summaries for the final official `testdraw2` and
`testsprite2` clients. The normalizer freezes extension discovery order,
extension request classes, MIT-SHM/core image transports, RANDR minor requests,
EWMH-related request classes, and zero unexpected errors. Client IDs, sequence
numbers, and recurring request counts are omitted because they are transport
observations rather than compatibility semantics. Raw JSONL traces remain in
the checksum-protected VM archive as evidence and are not repository goldens.

After a successful run, promote and validate its evidence with:

```sh
tests/compat/m12/promote_fixtures.py \
  --artifact-dir artifacts/vm/latest \
  --output-dir tests/fixtures/m12
tests/compat/m12/validate_fixtures.py tests/fixtures/m12
```

Promotion rejects a failed summary, an incorrect base or tested commit, missing
or unsafe archive members, checksum drift, failed probe results, malformed PPM
files, and non-identical software/GLES opaque scenes. It then records the exact
tested commit and SDL source hash in this document and regenerates
`SHA256SUMS` over the complete fixture set.
