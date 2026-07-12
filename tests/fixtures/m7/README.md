# Milestone 7 drawable fixtures

These fixtures describe the deterministic probe evidence for the first
server-rendered Glasswyrm window. `raw-events.json` records the required event
classes, while the XCB and restart JSON files define the stable machine-readable
probe results.

The reviewed final PPM, normalized `frames.jsonl` tail, and scene entry are
captured after the integrated software-content runtime is available. Regenerate
them only with:

```sh
./tools/gw-vm milestone7-runtime-test --yes
```

Extract `milestone7-rendering.tar`, compare the pixels semantically, and update
the fixture checksums explicitly. Normal tests never regenerate goldens.

