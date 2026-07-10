# Glasswyrm IPC

Milestone 3 introduces `libgwipc`, a local, versioned process-contract library.
It is independently buildable and installable, but no production Glasswyrm
process uses it yet.

- [GWIPC API 0](GWIPC_API_0.md) documents the installed C ABI and C++ wrappers.
- [GWIPC Wire Version 1](GWIPC_WIRE_V1.md) documents record encoding and the
  initial message registry.
- [Decision 0005](../decisions/0005-versioned-ipc-foundation.md) records the
  architectural boundary.

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
