# Milestone 14 physical hardware harness

This directory defines the reviewed, local-only input schema for the positive
VRR hardware run. Copy `config.example.toml` outside the source tree and replace
every placeholder with values reviewed for the target monitor and connector.

The schema is deliberately closed. It contains no command, SSH, package,
credential, token, or password fields. `gw-hw` rejects unknown keys, symlinked
configuration files, unsafe device names, invalid ranges, and malformed EDID
digests. `keyboard_device` and `pointer_device` must name two reviewed
`/dev/input/eventN` devices for the isolated session; the doctor verifies both
character devices before the live runner stops the selected getty.

The fixed live runner uses binaries from `/var/tmp/glasswyrm-build-m14` and
refuses symlinked or missing executables. Configure and build that directory
from the exact commit being accepted before moving to the target text VT.

```sh
./tools/gw-hw doctor --config /path/to/reviewed.toml
./tools/gw-hw milestone14-vrr-test \
  --config /path/to/reviewed.toml \
  --artifact-dir /var/tmp/glasswyrm-m14-hardware --yes
```

`doctor` is bounded discovery. The live command is deliberately disruptive: it
may take DRM master, stop the configured getty, switch VTs, and reconfigure the
selected display. Invoke it only from the configured spare text VT, with the
display manager inactive and independent recovery access available. Do not run
it from the current graphical session or a TTY whose interruption would be
noticeable.

The implementation includes deterministic parser, doctor, failure-unwind, and
fixture dry-run tests. Those are not positive hardware proof. Only a reviewed
live run whose cadence, property readback, images, restoration, checksums, and
archive validators all pass can satisfy the physical gate; no such acceptance
is claimed by this document.
