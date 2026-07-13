# Milestone 9 pinned-client fixtures

These reviewed 1024x768 headless frames were captured from the real
three-process Glasswyrm stack with xeyes 1.3.1 and xclock 1.2.0. The xclock
frames use `libgw_m9_fixed_time.so` and display 03:04 UTC. The combined frame
contains xeyes and analog xclock before xclock is intentionally terminated.

The normalized traces were produced from the matching raw JSONL traces with:

```sh
tests/compat/m9/m9_trace_summarize RAW.jsonl > tests/fixtures/m9/PROFILE.trace.json
```

Normal builds validate these files but do not regenerate them. Regeneration
requires repeating the exact application profiles in
`docs/compatibility/M9_CLIENT_AUDIT.md`, selecting the final reviewed compositor
frame, normalizing its raw trace with the command above, and then running:

```sh
(cd tests/fixtures/m9 && sha256sum *.ppm *.trace.json > SHA256SUMS)
```

The images show two white xeyes ellipses with black pupils, a white analog
clock face with black ticks and hands, a white digital `03:04` cell, and both
applications on the combined desktop. Black pixels elsewhere are the headless
output background.
