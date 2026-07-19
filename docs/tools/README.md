# Runtime tools

Milestone 13 replaces the `gwinfo` and `gwout` scaffolds with bounded same-UID
clients of the output-control socket:

- [`gwinfo`](M13_GWINFO.md) reports deterministic output and window snapshots
  in text or JSON.
- [`gwout`](M13_GWOUT.md) queries and atomically commits a complete output
  layout against its generation.
- [Milestone 14 VRR tools](M14_VRR_TOOLS.md) adds per-output policy edits,
  capability/decision/timing diagnostics, and fixed VM/hardware gates.

`gwctl`, `gwtrace`, and `gwbench` remain scaffold placeholders. These tools do
not expose arbitrary process execution, create output identities, or bypass
the server's capability and layout validation.
