# Milestone 11 pinned-xterm fixture

The accepted fixture will contain the reviewed normalized X11 trace from the
exact xterm patch 410 core-font ASCII profile documented in
`docs/compatibility/M11_XTERM_AUDIT.md`.

The fixture is intentionally pending until the complete clean-snapshot VM run
passes. A failed or diagnostic run is not an acceptable fixture source. After
that run, review the raw trace and normalize it with:

```sh
tests/compat/m11/m11_trace_summarize \
  artifacts/vm/latest/milestone11-xterm-trace.json \
  > tests/fixtures/m11/xterm.trace.json
```

Review the normalized request and event profiles against the source audit and
the interactive acceptance results. Then pin and validate it with:

```sh
(cd tests/fixtures/m11 && sha256sum xterm.trace.json > SHA256SUMS)
tests/tools/m11_fixture_validate.py tests/fixtures/m11
```

Normal builds must validate the accepted fixture but must never regenerate it.
The checksum and fixture will be added only after the live evidence is green
and reviewed.
