# Milestone 13 fixture staging

Status: bootstrap contract. The accepted fixture payload is promoted only from
the checksum-protected Milestone 13 Gentoo VM evidence archive.

`m13_scale_client` is the repository-owned raw-X11 proof of the experimental
`GW_SCALE` v0.1 client-buffer contract. A canonical two-output acceptance run
writes `scale-client-result.json` here with `--result` and captures the matching
`aware-left.ppm` and `aware-right.ppm` from the headless compositor.

The client always uses a 320x240 logical direct-root window and a 640x480
depth-24 pixmap at client buffer scale 2. It paints a deterministic four-pixel
checker with diagonal line accents, presents the pixmap, moves the logical
window, waits for `ScaleNotify`, verifies geometry and membership state, then
resets to legacy scale 1.

Fixture promotion must validate the result against
`scale-client-result.schema.json` and must not claim toolkit integration.

The complete accepted inventory also contains the output inventory, before and
after layouts, raw little- and big-endian RANDR/GW_SCALE results, legacy and
scale-aware per-output PPMs, transform frames, frame-set diagnostics, `gwinfo`
and `gwout` results, and the bounded software/GLES fractional comparison.
The VM gate validates the runtime `frame-sets.jsonl` against its native PPM
payloads, per-output FNV-1a hashes, and compositor aggregate hash before the
reviewed subset is eligible for promotion.

Regenerate only with this explicit command after a passing VM run:

```sh
tests/compat/m13/promote_fixtures.py \
  --artifact-dir artifacts/vm/latest \
  --output-dir tests/fixtures/m13
tests/compat/m13/validate_fixtures.py \
  --require-complete tests/fixtures/m13
```

Ordinary tests validate but never regenerate these files. Review logical and
physical geometry, output seams, every transform, and scale filtering before
accepting a promoted fixture set.
