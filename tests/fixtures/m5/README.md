# Milestone 5 policy fixtures

These files pin the deterministic JSON produced by every M5 synthetic policy
scenario. Ordinary tests compare output byte-for-byte and verify `SHA256SUMS`;
they never regenerate fixtures.

To deliberately regenerate the fixtures, build `gwm` and `gwm_m5_producer`,
then run each producer scenario against a fresh `gwm` instance with
`--max-commits 1` for the eight single-commit scenarios and `--max-commits 2`
for the remaining scenarios. Write each output to
`tests/fixtures/m5/SCENARIO.json`, then run:

```sh
cd tests/fixtures/m5
sha256sum *.json | sort -k2 >SHA256SUMS
```

Review every JSON semantic diff and the manifest diff before committing. A
hash change is evidence to inspect, not permission to refresh the golden.
