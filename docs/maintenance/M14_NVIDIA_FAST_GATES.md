# M14 NVIDIA fast software gates

`tools/gw-m14-dev` is the repository-owned entry point for the narrow software
gates used while converging NVIDIA VRR behavior. It never starts a physical
hardware run and refuses the frozen `/var/tmp/glasswyrm-build-m14` acceptance
build.

Configure a developer build explicitly:

```sh
./tools/gw-m14-dev configure-probe build-m14-probe
./tools/gw-m14-dev configure-stack build-m14-stack
```

Then select the smallest relevant gate:

```sh
./tools/gw-m14-dev timing build-m14-probe
./tools/gw-m14-dev probe build-m14-probe
./tools/gw-m14-dev presenter build-m14-stack
./tools/gw-m14-dev damage build-m14-stack
./tools/gw-m14-dev headless build-m14-stack
./tools/gw-m14-dev restart build-m14-stack
./tools/gw-m14-dev harness build-m14-stack
```

Every gate prints its exact compile targets and Meson test names, uses
`meson test --no-rebuild`, stops on the first failure, and retains output under
`BUILD/meson-logs/m14-dev-GATE.log`.

`changed BUILD BASE_REF` prints each changed-path mapping before running the
deduplicated gates. Unknown paths, public headers, and Meson changes map to the
conservative `integration` gate. Review that printed mapping before using its
result as evidence.

The explicit `integration` command is the only broad command. It compiles the
named M14 stack and probe targets and runs the integrated 180-frame, restart,
harness, and source-layout checks. It does not replace the separately scheduled
Clang, sanitizer, VM, or physical matrices.
