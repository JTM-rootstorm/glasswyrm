# Milestone 13 source-layout audit

Status: implementation layout passes; final acceptance remains a separate VM
and validation gate.

Required baseline: `d3440d3b8df1533410a9a2c4be46f2eea0cfb88d`

## Guard and current result

`tests/tools/source_layout_test.sh` classifies C/C++ source and headers under
`src/` against the required Milestone 13 base commit. It enforces the
established physical-line budgets: 1,000 lines for unchanged files, 600 for
new or materially rewritten files, 500 for coordinators, and 250 for
`main.cpp`. It
also reports ordinary functions above 100 lines for review above 150 and
requires exact, non-stale entries in
`docs/maintenance/source_size_allowlist.txt`.

The current M13 run passes with an empty active allowlist, no file-budget,
coordinator, main, routing-shell, or stale-allowlist failure, and nine advisory
function review items. Advisory spans do not weaken the machine-enforced file
budgets.

## Milestone 13 decomposition

| Responsibility | Primary implementation areas | Boundary |
| --- | --- | --- |
| Output types, validation, identity, mapping | `src/output/model/` | Pure component-neutral state and checked geometry. |
| Backend inventory | `src/backends/headless/`, `src/backends/drm/` | Backend facts remain below compositor inventory publication. |
| Output transaction | `src/glasswyrmd/output_*` | Control parsing, policy staging, promotion, and rollback are separated from X11 dispatch. |
| Dynamic X11 and RANDR state | `src/glasswyrmd/screen_*`, `randr_*`, focused extension handlers | Protocol handles and event encoding do not own output policy. |
| Multi-output policy | `src/wm/`, `src/gwm/` | Pure placement and assignment remain distinct from the GWIPC reactor. |
| Scene membership and frame sets | `src/compositor/`, `src/gwcomp/` | Scene validation, rendering, presentation, and manifest serialization are focused modules. |
| Scale and transform rendering | `src/render/software/`, `src/render/gles/` | Canonical mapping and per-renderer orchestration remain behind the output renderer interface. |
| Control clients | `tools/output_client/`, `tools/gwinfo/`, `tools/gwout/` | Shared query/format/edit mechanics avoid duplicated CLI protocols. |
| Session and VM acceptance | `src/session/`, `tools/gw-vm.d/lib/milestone13.sh`, `tests/compat/m13/` | Runtime argv construction remains shell-free; fixed acceptance scripting stays outside runtime. |

The M13 test registry is also split into focused Meson submanifests under
`tests/manifest/`, so adding or reading a subsystem test no longer requires
loading one monolithic test file.

## Reviewed spans

The advisory scan reports nine spans above 150 lines: scene commit,
server input and lifecycle coordination, the request routing shell, runtime
bridge coordination, compositor option parsing, two IPC validation/correlation
paths, and the GLES multi-output render orchestration. Each keeps one ordered
transaction or flat validation/routing responsibility while delegating feature
semantics to focused helpers. Any further growth requires another decomposition
review rather than an allowlist exception.

This audit proves source organization only. Strict compiler, sanitizer,
component, historical regression, and clean Gentoo VM gates independently
prove build and runtime behavior.
