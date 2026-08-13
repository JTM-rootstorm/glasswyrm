# Rust Transition Source Map

Status: R0 inventory at carried-forward source
`a39c7788bb5226e793698192028b9a4a57202f8d`

Behavior/evidence anchor:
`36009a8bbe50794d6808d142ac57b623062a1e63`

## Purpose

This map assigns every current runtime source family to an intended Rust crate
or an explicitly retained native boundary. It is a migration routing document,
not permission to move several subsystems at once. Refine a row to individual
files before starting that row, and preserve the legacy implementation as the
oracle until its compatibility gate passes.

The current native implementation is approximately 78,500 lines across
`src/`, `include/`, and compiled `tools/`. Directory globs below include both
implementation and header files unless an exception is named.

## Target dependency direction

```text
gw-types
   |
gw-wire
   |
gw-ipc
   +----------------+------------------+
   |                |                  |
gwm-core       gwcomp-core       glasswyrm-x11
   |                |                  |
  gwm             gwcomp         glasswyrm-core
                                         |
                                    glasswyrmd

gw-platform-linux
        |
      gw-sys
```

`gw-types` and `gw-wire` do not depend on a runtime process. `gwm-core` and
`gwcomp-core` remain hardware-free. `glasswyrm-x11` owns protocol-domain
encoding and decoding, not daemon startup. `gw-sys` is the unsafe quarantine
zone; a retained C shim belongs beside that boundary, not in a policy crate.

## Source assignments

| Current source family | Primary target | Migration boundary |
|---|---|---|
| `include/glasswyrm/ipc/types.h`, `version.h`, public IDs/enums in `contracts.h` | `gw-types`, temporary `gw-ipc-capi` | Preserve numeric values, widths, signedness, API 0.9, wire 1.0, and SOVERSION 0. |
| Remaining `include/glasswyrm/ipc/*.h`, `ipc.h`, `ipc.hpp` | `gw-ipc`, temporary `gw-ipc-capi` | Installed C/C++ ABI remains until consumers and packaging deliberately move. Do not make Rust layout the wire layout. |
| `src/ipc/wire/*` | `gw-wire` | Port byte readers/writers, envelope framing, and every lifecycle, policy, input, compositor, output, session, control, and VRR contract against canonical bytes. |
| `src/ipc/message.cpp`, `contract_api.cpp`, `control_api.cpp`, `public_api.cpp` | `gw-types`, `gw-wire`, `gw-ipc-capi` | Split value/wire logic from exported C ABI rather than preserving the current compilation-unit shape. |
| `src/ipc/connection*`, `endpoint*`, `internal.hpp` | `gw-ipc` | Own Unix transport, hello/version negotiation, queues, correlation, FD passing, disconnect behavior, and validation. |
| `src/wm/*` | `gwm-core` | Pure placement, focus, stacking, output assignment, interactive policy, transactions, hashes, and VRR eligibility. No sockets or process startup. |
| `src/gwm/*` | `gwm` | Process options, signals, GWIPC peer handling, snapshots, and dispatch around `gwm-core`. This is the first runtime replacement. |
| `src/output/model/*`, `src/output/vrr/*` | `gw-types`, `gwcomp-core`, with shared policy inputs in `gwm-core` | Split stable output/value types from compositor authority and WM candidate policy; preserve M13/M14 semantics. |
| `src/compositor/*` | `gwcomp-core` | Scene, surfaces, buffers, damage, validation, output damage, and final VRR state remain compositor-owned and headless-testable. Replace embedded C `gwipc_*` structs with explicit Rust-domain types only after byte/API equivalence is proven. |
| `src/backends/headless/*`, `src/backends/output/*` | `gwcomp-core`, `gwcomp` | Implement the Rust headless/reference backend before real DRM. Keep deterministic frame and simulated-VRR outputs, while retaining their non-hardware evidence label. |
| `src/render/software/*`, renderer interfaces in `src/render/*.hpp` | `gwcomp-core` | Rust reference rendering and golden parity first; measured native/assembly optimization only later. |
| `src/gwcomp/*` except raw DRM entry points | `gwcomp` | Process shell, options, reactor, contracts, inventory publication, session coordination, scene manifest, presentation transactions, and renderer selection. |
| `src/session/*` | `gw-tools` process shell over `gw-platform-linux` | Migrate the unprivileged session launcher and process supervisor without moving protocol, WM, or display authority into it. |
| `src/protocol/x11/*` | `glasswyrm-x11` | X11 framing, byte order, setup, request/reply/event codecs and protocol-domain types. Preserve malformed-input and ordering behavior. |
| `src/core/*` | `gw-types` or `glasswyrm-core` | General geometry may be shared only if it stays dependency-light; resource/server ownership belongs in `glasswyrm-core`. |
| `src/glasswyrmd/request_handlers/*`, `extensions/*`, wire/dispatcher files | `glasswyrm-x11`, `glasswyrm-core` | Separate decode/encode and extension registries from resource mutations and policy. Migrate only the supported compatibility subsets. |
| `src/glasswyrmd/*` resource stores, lifecycle, projection, routing, raster and input-state files | `glasswyrm-core` | Server/resource truth, event routing, drawable state, snapshots, peer projections, and current reference raster behavior. Split by ownership rather than mirroring legacy files. |
| `src/glasswyrmd/main.cpp`, options, listeners, reactor, signals, shutdown and peer transports | `glasswyrmd` | Thin daemon shell after `glasswyrm-x11` and `glasswyrm-core` are ready. |
| `src/tools/*`, `tools/gwctl`, `gwinfo`, `gwout`, `gwtrace`, `output_client/*` | `gw-tools` | Early real Rust GWIPC consumers. Preserve CLI text/JSON, exit status, and same-UID control behavior. |
| `tools/gwbench` | `gw-tools` or a dedicated benchmark target | Migrate only when the measured path it exercises has moved; do not make it a runtime dependency. |
| `tools/gw-vm*`, `gw-m14-dev*`, `gw-hw*`, provenance generators, and `scripts/*` | Retained build/test/acceptance tooling | Python and shell remain acceptable outside the runtime. Keep guard, provenance, restoration, and VM behavior independently tested; migrate only for a concrete maintenance benefit. |
| `src/scaffold/*`, `include/glasswyrm/scaffold/*` | Remove after replacement | Historical scaffold has no permanent crate assignment. |
| README-only placeholders under `src/backends/nested`, `src/extensions`, `src/platform`, `src/render/gl`, and `src/render/vulkan` | Documentation or removal | Do not create empty crates to preserve placeholder symmetry. |

## Native and unsafe boundary assignments

| Current source family | Target | Rule |
|---|---|---|
| `src/backends/drm/*` | `gw-platform-linux` over `gw-sys`, with a retained C shim only where justified | Keep discovery, policy-neutral state translation, and safe ownership in Rust. Raw libdrm calls, callbacks, and ABI records stay narrow. Real backend migrates after headless `gwcomp`. |
| `src/gwcomp/drm_runtime*` | `gwcomp` over `gw-platform-linux` | Keep backend selection and compositor transaction orchestration in `gwcomp`; move raw DRM/session operations below the safe platform interface. |
| `src/backends/session/*` | `gw-platform-linux` / `gw-sys` | Preserve ordered KMS/KD/VT restoration and explicit external-session behavior. Never weaken the physical guard to simplify FFI. |
| `src/input/real_libinput_api.cpp`, `libinput_backend*`, `xkb_keymap*` | `gw-platform-linux` / `gw-sys` | Quarantine libinput/udev/xkb ABI access; keep routing and repeat policy outside the unsafe layer. |
| Other `src/input/*` | `glasswyrm-core` | Cursor/input state, allowlist policy, routing, and deterministic fake APIs remain headless-testable. |
| `src/render/gles/*`, `renderer_factory*` native selection | `gw-platform-linux` / `gw-sys`, orchestrated by `gwcomp` | Isolate GBM/EGL/GLES handles and callbacks. Software remains the canonical reference. |
| `src/tools/drm_probe*`, `drm_vrr_probe*` | `gw-platform-linux` plus guarded `gw-tools` binaries | These remain hardware-capable and must require explicit authorization when they can mutate or take display authority. |
| Existing C fixtures and public C ABI | Retained C or `gw-ipc-capi` during migration | Compatibility surface, not precedent for new high-level C. |
| Future x86_64 assembly | Optional native leaf below a Rust/C reference | Only after profiling and golden equivalence; never protocol, policy, IPC, KMS state, or input routing. |

## Known coupling hazards

These are interface work, not reasons to merge crates:

- `src/gwcomp/output_inventory_publisher.cpp` is compiled into both server and
  compositor test/production contexts. Define the inventory value and wire
  representation once, then leave publication ownership with the appropriate
  process.
- `src/wm/interactive_policy.cpp` is consumed from both `glasswyrmd` and `gwm`.
  Move deterministic policy into `gwm-core`; server code should communicate
  decisions rather than acquire WM policy authority.
- compositor, output, and process types currently embed public C `gwipc_*`
  structs. Rust domain types must not inherit C memory layout as their wire
  format. Convert explicitly at the compatibility boundary.
- output types cross `glasswyrmd`, `gwm`, and `gwcomp`, but authority does not:
  server coordinates, WM selects candidates, and compositor validates and
  commits final display state.
- DRM presentation currently joins compositor transactions, session recovery,
  timing evidence, and real libdrm calls. First extract a recorded/mock backend
  contract; move the real backend last.
- helpers under `tests/helpers/` are coupled to C++ process fixtures. Port a
  harness against legacy binaries before using it to validate Rust binaries.

## Migration order and deletion gates

1. Establish `gw-types`, `gw-wire`, `gw-ipc`, `gw-test-support`, and orchestration.
2. Prove Rust/legacy GWIPC compatibility in both directions.
3. Replace `gwm-core` and `gwm`; delete legacy `src/wm/` and `src/gwm/` only
   after mixed-process snapshot/replay parity.
4. Migrate control tools as real Rust GWIPC consumers.
5. Replace headless/software `gwcomp`; keep real DRM outside the edit loop.
6. Replace X11 codec, server core, and `glasswyrmd` process shell by explicit
   compatibility subsets.
7. Converge DRM, input, session, and GLES native boundaries.
8. Remove production C++ and decide whether the remaining native leaf justifies
   Meson. Git history, not dead source copies, is the rollback record.

At every deletion gate, use the process combinations documented by the
transition decision. Do not maintain every theoretical Rust/legacy topology.
