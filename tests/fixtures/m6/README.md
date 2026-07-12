# Milestone 6 Fixtures

These fixtures describe the deterministic metadata-only lifecycle boundary.
They contain no PPM or client pixel data.

- `structural-events.json` records exact 32-byte little- and big-endian vectors
  for MapNotify, UnmapNotify, ConfigureNotify, and DestroyNotify.
- `scene-manifest.jsonl` is the canonical two-window metadata scene used by the
  fixture validation test.
- `xcb-result.schema.json` defines the deterministic result fields expected
  from the repository XCB lifecycle probe.
- `restart-result.json` is the exact successful result written by
  `m6_restart_hold_probe`.
- `SHA256SUMS` covers every fixture other than itself.

Validate with `meson test -C BUILD m6-fixtures` or run
`tests/tools/m6_fixture_validate.sh tests/fixtures/m6` directly.

