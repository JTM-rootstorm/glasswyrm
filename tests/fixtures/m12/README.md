# Milestone 12 compatibility fixtures

Status: accepted deterministic fixtures for the exact SDL 2.32.10 X11
software-renderer profile documented in `docs/compatibility/M12_SDL.md`.

These files were promoted from the checksum-protected full Gentoo VM evidence
archive captured at commit `6a7697a6ac48a2d9a6c3d23ce9e58343ac5037cc` on the required base
`ae6b6c93a29a1fb985dcea8455650d15c0fec364`. The SDL source archive SHA-256 was
`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.

`registry-little.json`, `registry-big.json`, and `sdl-probe.json` retain the
repository probe results. `extensions.json`, `testdraw2.trace.json`, and
`testsprite2.trace.json` are bounded normalized summaries; the raw JSONL trace
remains binary acceptance evidence and is deliberately not a golden fixture.
The PPM pairs are byte-identical opaque software/GLES outputs. Renderer and DRM
JSONL files retain the accepted runtime diagnostics used to validate damage,
synchronization, VT recovery, and restoration.

`result-contract.json` freezes the required probe/workload matrix.
`SHA256SUMS` protects every ordinary fixture and this provenance document.
Regenerate this directory only from a new fully accepted run:

```sh
tests/compat/m12/promote_fixtures.py \
  --artifact-dir artifacts/vm/latest \
  --output-dir tests/fixtures/m12
tests/compat/m12/validate_fixtures.py tests/fixtures/m12
```

Do not promote a failed run, hand-edit normalized traces, manufacture frames,
or replace raw runtime evidence with repository goldens.
