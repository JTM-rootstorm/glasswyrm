# Milestone 13 output and scaling fixtures

Status: accepted deterministic fixtures for the experimental Milestone 13
output-model and scaling profile.

These fixtures were promoted from the checksum-protected Gentoo VM evidence
archive captured at commit `1cb51cafcbd38e8bd53e7b9cfe8bba91be283d97` on required base
`d3440d3b8df1533410a9a2c4be46f2eea0cfb88d`. They cover the canonical two-output headless layout, raw
little- and big-endian RANDR/GW_SCALE probes, legacy and scale-aware surfaces,
all required transforms, output tools, and software/GLES fractional evidence.

Regenerate only from a newly accepted VM run, then review output geometry,
seams, transforms, and scale filtering before committing:

```sh
tests/compat/m13/promote_fixtures.py \
  --artifact-dir artifacts/vm/latest \
  --output-dir tests/fixtures/m13
tests/compat/m13/validate_fixtures.py \
  --require-complete tests/fixtures/m13
```

Do not regenerate these fixtures in ordinary tests. The repository client is a
raw protocol proof and does not imply toolkit GW_SCALE integration.
