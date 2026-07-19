# `gwout` output configuration

`gwout` queries the complete current inventory before making any change. A
`set` command edits one known output, derives its logical dimensions without
approximating the requested scale, validates the complete layout locally, and
sends the complete output snapshot followed by a generation-guarded commit.

```sh
gwout --socket /run/glasswyrm/control.sock list --json
gwout --socket /run/glasswyrm/control.sock set RIGHT \
  --position 640,0 --scale 5/4
```

The client rejects overlapping layouts, negative enabled-output positions,
unsupported scales or transforms, disabling the last output, and disabling
the primary output without choosing another. M13 rejects DRM mode changes;
headless modes remain bounded by the published descriptor.

A stale-generation acknowledgement exits nonzero and reports the current
generation. Other server rejections also exit nonzero. No partial-update
request or output-creation request is sent.

Because `gwinfo` and `gwout` are real clients, the Meson policy is explicit:
`-Dtools=true` requires `-Dlibgwipc=true`. `gwctl`, `gwtrace`, and `gwbench`
remain scaffold placeholders in M13.
