# Glasswyrm

Glasswyrm is a from-scratch, local-first display-stack project for modern Linux. It explores useful local desktop workloads through selective, explicitly tested X11-compatible profiles while keeping protocol compatibility separate from its internal architecture.

Glasswyrm is X11-compatible where useful rather than an Xorg reimplementation. Authoritative protocol and compatibility profiles define the supported boundaries. Its focus is a clean, observable display system with explicit policy, deterministic behavior, and foundations for modern output features such as per-output scaling, variable refresh rate, HDR, and color management.

## Intent

Glasswyrm is Rust-first by design. Rust is the default for new and migrated high-level protocol, state, policy, IPC, orchestration, and rendering code; C is reserved for bounded platform or ABI shims; C++ is migration-only; and assembly is limited to measured optional optimizations with a reference implementation.

Glasswyrm uses independently testable processes with distinct authority:

- `glasswyrmd` owns X11 protocol truth and client-visible state.
- `gwm` owns window-management policy.
- `gwcomp` owns composition, presentation, and final display authority.

In integrated mode, `glasswyrmd` connects independently to `gwm` and `gwcomp` over GWIPC; there is no direct `gwm`-to-`gwcomp` control path or shared process authority. Lifecycle state becomes committed and client-visible only after the required policy and compositor acknowledgements succeed, with bounded snapshots supporting rollback and replay.

GWIPC is an explicit, versioned local protocol. X11 compatibility remains an external contract rather than an internal design constraint, and optional Glasswyrm extensions are capability-negotiated instead of silently changing baseline behavior.

Headless and software paths provide deterministic reference behavior before platform-specific display and input backends are exercised. Linux DRM/KMS, input, and accelerated-rendering integrations remain behind explicit ownership and restoration boundaries.

Glasswyrm is a local research stack, not a hardened multi-user display server. The documented X11 profiles do not implement Xauthority or MIT-MAGIC-COOKIE-1. Where same-user checks apply, they do not isolate processes sharing an account. Diagnostic artifacts may contain sensitive metadata or pixels. Physical hardware paths require explicit operator authorization; headless and software paths are the normal development route.

## Ecosystem boundary

Glasswyrm owns window-system protocol behavior, window-management policy, composition, input and output policy, and display effects. [Prismdrake](https://github.com/JTM-rootstorm/prismdrake-de) is a separate optional consumer that owns session and shell presentation, settings, notifications, and user-facing fallback choices.

Glasswyrm's primary target is Gentoo Linux on x86_64. A Wyrmroot-native backend is explicitly deferred; no Wyrmroot support or compatibility claim is made here.

## Documentation

- [Project specification](docs/GLASSWYRM_SPEC.md)
- [Architecture notes](docs/architecture/README.md)
- [Architecture decisions](docs/decisions/README.md)
- [Rust-first architecture decision](docs/decisions/0017-rust-first-transition.md)
- [X11 protocol profiles](docs/protocols/README.md)
- [Inter-process protocols](docs/ipc/README.md)
- [Output architecture](docs/output/README.md)
- [Compatibility profiles](docs/compatibility/README.md)
- [Session documentation](docs/session/README.md)
- [Runtime tools](docs/tools/README.md)

## License

Glasswyrm is licensed under the [GNU General Public License v3.0-only](LICENSE).
