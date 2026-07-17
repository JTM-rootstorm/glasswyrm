# Milestone 11 pinned-xterm fixture

Status: accepted deterministic trace fixture for the exact xterm patch 410
core-font ASCII profile documented in
`docs/compatibility/M11_XTERM_AUDIT.md`.

`xterm.trace.json` is the reviewed normalized X11 trace captured by two full
Gentoo VM runs at commit
`eb8a20f76b24cc7c07459a402603bad5e7b6cc39`. Both runs recorded
`scenario_exit=0`, `run_mode=full`, `normalized_trace=passed`,
`exact_trace=bootstrap`, and `archive_validation=passed`. Their independently
captured raw JSONL traces normalized to byte-identical JSON with SHA-256:

```text
89df372b629e8c10248ff5223ae9389d1fc59737082b7c97bb0db8e11416dd73
```

The normalizer records `ImageText8` and opcode 76 as presence-only counts of
one. xterm may coalesce redundant text redraws without changing delivered
events, PTY output, or the canonical frame; the three reviewed raw captures
contained 800 or 806 `ImageText8` requests while every other normalized field
and every screenshot matched. The raw JSONL and checksum-protected evidence
archives retain the exact per-run volume.

The reviewed capture directories were `artifacts/vm/m11-fixture-a` and
`artifacts/vm/m11-fixture-b`. Those ignored runtime artifacts are provenance,
not repository fixtures; this directory contains the accepted normalized
result and its checksum.

To review a future candidate, normalize the raw JSONL artifact from each full
run. The summarizer input must be `milestone11-xterm-trace.raw.jsonl`, not the
already-normalized `.json` artifact:

```sh
tests/compat/m11/m11_trace_summarize \
  artifacts/vm/m11-fixture-a/milestone11-xterm-trace.raw.jsonl \
  > /tmp/m11-fixture-a.json
tests/compat/m11/m11_trace_summarize \
  artifacts/vm/m11-fixture-b/milestone11-xterm-trace.raw.jsonl \
  > /tmp/m11-fixture-b.json
cmp /tmp/m11-fixture-a.json /tmp/m11-fixture-b.json
```

Review the normalized request and event profiles against the source audit and
interactive acceptance results before replacing the fixture. Then regenerate
and validate the checksum deliberately:

```sh
(cd tests/fixtures/m11 && sha256sum xterm.trace.json > SHA256SUMS)
tests/tools/m11_fixture_validate.py tests/fixtures/m11
```

Normal builds validate the accepted fixture unconditionally but never
regenerate it. After changing the fixture, commit it first and run the complete
clean `milestone11-runtime-test` at that exact commit. Milestone acceptance
requires the live normalized trace to match this fixture with
`exact_trace=passed`; the two bootstrap captures alone are not the final clean
acceptance run.
