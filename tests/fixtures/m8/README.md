# Milestone 8 Golden Fixture

This fixture records the reviewed synthetic-input routing and deterministic
headless rendering accepted for Milestone 8. IDs and logical times are
protocol values; no wall-clock timestamps are present.

`final.ppm` is the settled colored frame produced by the canonical two-client
XCB script before client cleanup. It shows the blue A window with red and
yellow response regions and the topmost green B window with magenta and white
response regions. No cursor sprite is rendered.

Normal tests only validate this checked-in evidence. To regenerate explicitly,
run the integrated M8 runtime, execute `xcb_milestone8_probe`, select the final
non-cleanup frame from `frames.jsonl`, and run:

```sh
./tests/tools/m8_fixture_regenerate.sh \
  /path/to/captured-m8-artifacts tests/fixtures/m8
```

Review every event target, coordinate, state, sequence, focus/crossing detail,
frame hash, and pixel region before committing regenerated files.

