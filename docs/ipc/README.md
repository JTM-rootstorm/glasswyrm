# Glasswyrm IPC

`libgwipc` is Glasswyrm's local, versioned process-contract library. Milestone 3
established its transport, Milestone 4 added the compositor contract used by
`gwcomp`, and Milestone 5 adds typed snapshot controls and the capability-gated
window-policy contract used by `gwm`.

- [GWIPC API 0](GWIPC_API_0.md) documents the installed C ABI and C++ wrappers.
- [GWIPC Wire Version 1](GWIPC_WIRE_V1.md) documents record encoding and the
  additive message registry.
- [M6 Lifecycle Contract](M6_LIFECYCLE_CONTRACT.md) documents API 0.4 lifecycle
  and metadata-only records.
- [M8 Synthetic-Input Contract](M8_SYNTHETIC_INPUT_CONTRACT.md) documents the
  additive API 0.5 DiagnosticTool input vocabulary.
- [GWIPC API 0](GWIPC_API_0.md) and [wire version 1](GWIPC_WIRE_V1.md) also
  record API 0.6 session-state, interactive-policy, and cursor-surface
  additions plus API 0.7 eventfd CPU-buffer synchronization.
- [Decision 0005](../decisions/0005-versioned-ipc-foundation.md) records the
  architectural boundary.
- [Decision 0007](../decisions/0007-window-manager-policy-scaffold.md) records
  the M5 listener and additive policy-contract boundary.
- [Window-manager policy](../wm/README.md) documents the consumer-facing M5
  policy service and deterministic algorithm.

Run the IPC-only proof with:

```sh
meson setup build-ipc-only \
  -Dlibgwipc=true -Dglasswyrmd=false -Dgwm=false -Dgwcomp=false -Dtools=false
meson compile -C build-ipc-only
meson test -C build-ipc-only --print-errorlogs
```

The VM acceptance command is:

```sh
./tools/gw-vm milestone3-runtime-test --yes
```

The M4 and M5 process gates are separate fixed commands. The M5 harness exists,
but a real Gentoo guest result is still required before M5 is declared complete:

```sh
./tools/gw-vm milestone4-runtime-test --yes
./tools/gw-vm milestone5-runtime-test --yes
```
