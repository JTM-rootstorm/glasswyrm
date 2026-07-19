# Milestone 14 hardware harness skeleton

This directory defines the reviewed, local-only input schema for the positive
VRR hardware run. Copy `config.example.toml` outside the source tree and replace
every placeholder with values reviewed for the target monitor and connector.

The schema is deliberately closed. It contains no command, SSH, package,
credential, token, or password fields. `gw-hw` rejects unknown keys, symlinked
configuration files, unsafe device names, invalid ranges, and malformed EDID
digests.

```sh
./tools/gw-hw doctor --config /path/to/reviewed.toml
./tools/gw-hw milestone14-vrr-test --config /path/to/reviewed.toml --yes
```

The current command is a pre-integration skeleton. It performs bounded
read-only checks and intentionally does not change KMS, VT, getty, or session
state. Its output is not hardware proof and no final fixture is generated.
