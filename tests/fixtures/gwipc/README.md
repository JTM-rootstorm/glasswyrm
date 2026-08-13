# Canonical GWIPC interoperability fixtures

These fixtures externalize exact byte goldens that were already accepted by
the legacy C++ codec tests. Files use strict lowercase hexadecimal with one
record per file and a single trailing newline. They are compatibility oracles,
not generated Rust layouts.

`manifest.tsv` has these tab-separated columns:

1. `name`: stable probe and test identifier
2. `message_type`: GWIPC message ID in hexadecimal
3. `wire_kind`: `payload` or a complete `record`
4. `wire_version`: the applicable GWIPC wire version
5. `hex_file`: path relative to this directory
6. `fd_count`: ancillary descriptor count required by the contract
7. `expected_decode`: `ok` or `reject` for a conforming decoder
8. `meaning`: short description of the represented value or defect

The manifest contains exactly one accepted payload fixture for each of the 52
GWIPC payload codecs. The Rust matrix reads the manifest and fixture files at
test time, decodes every legacy-produced payload, and requires the Rust encoder
to reproduce the exact bytes. The legacy probe then decodes those same
canonical Rust bytes, establishing compatibility in both directions without
duplicating byte arrays in either test.

The legacy probe accepts `encode NAME OUTPUT`, `decode NAME INPUT`, and
`verify FIXTURE_ROOT`. `encode` constructs the named value through the legacy
codec. `decode` returns nonzero for malformed or noncanonical input. `verify`
proves every checked-in fixture still matches the legacy encoder and expected
decode outcome. Fixtures prefixed `rust-malformed-` are mutations constructed
and checked by the Rust matrix; the legacy verifier must reject them.
`SHA256SUMS` covers the manifest and every hexadecimal record.
