# Milestone 12 Extension Registry

Milestone 12 adds one immutable registry gated by the `--game-compat` server
profile. Without that profile, `QueryExtension` and `ListExtensions` retain the
historical all-absent behavior. Exact, case-sensitive registry names are the
only accepted names; aliases are not added.

| Extension | Major opcode | First event | Events | First error | Errors | Maximum version |
|---|---:|---:|---:|---:|---:|---:|
| BIG-REQUESTS | 128 | 0 | 0 | 0 | 0 | 1.0 |
| MIT-SHM | 129 | 64 | 1 | 128 | 1 | 1.1 |
| XFIXES | 130 | 65 | 1 | 129 | 1 | 2.0 |
| DAMAGE | 131 | 66 | 1 | 130 | 1 | 1.1 |
| RENDER | 132 | 0 | 0 | 131 | 5 | 0.11 |
| COMPOSITE | 133 | 0 | 0 | 0 | 0 | 0.4 |
| RANDR | 134 | 67 | 2 | 136 | 3 | 1.3 |

`ListExtensions` returns enabled names in ascending major-opcode order. A
per-extension disable switch can remove a registry entry without changing any
remaining assignment. Requests use the registry major opcode in byte zero and
the extension minor opcode in byte one, then enter the extension dispatcher
rather than the core request switch.

The registry compile-time check rejects overlapping event or error ranges.
Generic encoders preserve the recipient byte order, sequence, request
major/minor metadata, and the `SendEvent` event bit. Each extension module owns
its request lengths, resource validation, version negotiation, and deferred
minor-opcode errors.

This registry is a stability contract for the Milestone 12 profile, not an
allocation mechanism for arbitrary future extensions.
