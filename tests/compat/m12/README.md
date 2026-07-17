# Milestone 12 external-client probes

This directory owns the exact SDL 2.32.10 compatibility profile. It does not
replace the official SDL programs: `testdraw2` and `testsprite2` are built
unmodified from the verified release archive and remain mandatory workloads.

Repository tools:

- `verify_manifest.py` validates the pinned source, detached signature, source
  files, build arguments, environment, and official-program hashes.
- `acquire_sdl.sh` downloads the official archive, detached signature, and
  signing-key page; verifies both SHA-256 values and the exact signing-key
  fingerprint; then verifies the detached signature in an isolated keyring.
- `build_clients.sh` performs the frozen CMake/Ninja SDL build and builds the
  repository raw, XCB, and public-SDL probes.
- `m12_raw_probe.py` exercises the complete registry, BIG-REQUESTS, safe
  same-UID MIT-SHM transport, XFIXES/DAMAGE, exact RENDER pixels,
  COMPOSITE named-pixmap lifetime, RANDR reporting/errors, and malformed
  client isolation in both client byte orders.
- `m12_xcb_probe.c` and its focused graphics helper exercise every supported
  extension family through the public XCB extension bindings.
- `m12_sdl_probe.c` uses only public SDL APIs for display, software surface,
  clipboard, cursor, fullscreen-desktop, borderless, restore, and close paths.
- `run_workloads.py` runs the fixed SHM or no-SHM matrix and emits deterministic
  result JSON. The official programs are intentionally bounded by the harness;
  their unmodified event loops are not patched to exit.
- `validate_result.py` and `validate_fixtures.py` reject incomplete evidence.
- `normalize_fixture_traces.py` selects the final ordered official SDL clients
  and removes transport-only IDs, sequences, and recurring counts while
  retaining extension, image-transport, RANDR, EWMH-class, and error evidence.
- `promote_fixtures.py` accepts only a checksum-valid evidence archive paired
  with a fully passing M12 summary, stages the Section 37 fixture set, and
  records its exact provenance. Raw traces remain runtime evidence.

Typical build shape:

```sh
tests/compat/m12/acquire_sdl.sh /var/tmp/glasswyrm-m12-clients/download
tests/compat/m12/build_clients.sh \
  /var/tmp/glasswyrm-m12-clients/download/SDL2-2.32.10.tar.gz \
  /var/tmp/glasswyrm-m12-clients/source \
  /var/tmp/glasswyrm-m12-clients/build \
  /var/tmp/glasswyrm-m12-clients/prefix
```

The runtime harness starts Glasswyrm before invoking `run_workloads.py`. The
`no-shm` profile is valid only against a server explicitly launched with
`--disable-extension MIT-SHM`; the raw registry and extended PutImage checks
make that fallback observable instead of inferred.
