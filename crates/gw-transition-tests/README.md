# Rust transition process gates

This crate contains narrow process-level compatibility gates that run Rust test
drivers against the retained C++ binaries. It is test infrastructure, not a
runtime component.

The `legacy-output-restart` binary ports the accepted M14 compositor-restart
case from `tests/integration/output_configuration_process_test.cpp`. It starts
the legacy `glasswyrmd`, `gwm`, and headless `gwcomp`, submits configuration 601
while the compositor peer is being replaced, verifies rejection retains layout
generation 1, then verifies configuration 602 is accepted as generation 2.

It uses bounded polling for socket creation, stopped process state, server
quiescence, replies, and process exit. Failures produce a machine-readable
bundle containing command lines, process status, stdout, stderr, and a JSONL
scenario trace.

Run it from the repository root after building the legacy stack with the
experimental M14 features enabled:

```sh
cargo run -p gw-transition-tests --bin legacy-output-restart -- \
  --build-dir build-rust-transition-baseline
```

`GW_LEGACY_BUILD_DIR` may be used instead of `--build-dir`.
`GW_TRANSITION_ARTIFACT_ROOT` selects the failure-bundle root.
No DRM device or physical display is used; the compositor backend is headless.
