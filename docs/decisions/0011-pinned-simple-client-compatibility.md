# 0011: Pin Simple Clients and Flatten Child Subtrees

Status: Accepted; external application acceptance pending

## Context

Milestone 9 targets the first unmodified X11 applications. A vague goal such
as “support xeyes and xclock” would permit client-version drift, accidental
extension dependencies, and compatibility code with no observed caller. Xt
and Xaw also use child windows, while the established GWIPC contract publishes
one compositor surface for each managed top-level window.

## Decision

Compatibility is tied to `xeyes` 1.3.1 and `xclock` 1.2.0 with the exact core
profiles in `tests/compat/m9/clients.toml`. Shape and Render remain absent.
Request scope comes from those pinned source paths and reviewed normalized
traces. The server provides one deterministic built-in fixed font and a
test-only realtime preload for fixed xclock frames.

`glasswyrmd` keeps protocol truth and recursively flattens viewable
InputOutput descendants into the opaque pixel buffer of their direct-root
top-level ancestor. Each child is clipped by its parent and the top-level
bounds. InputOnly and unviewable windows do not paint. `gwcomp` continues to
receive and compose only top-level surfaces; it does not learn X11 child
semantics or become an X11 client.

GWIPC API 0.5, SOVERSION 0, and wire version 1.0 remain unchanged. Existing
buffer publication, damage, release, and replay messages already express the
resulting top-level image, so a child-surface message would add a second
authority without adding required information.

## Consequences

- Child backing and X11 stacking stay private to the protocol server.
- The compositor-facing contract remains stable and prior producers continue
  to work unchanged.
- Recomposition may copy a complete bounded top-level buffer; later profiling
  may justify incremental subtree work without changing the authority split.
- Broader fonts, extensions, arbitrary applications, and application profiles
  not proven by reviewed traces remain unsupported.
- M9 is not complete until pinned binary versions, source hashes, normalized
  traces, exact frames, and the Gentoo VM gate are reviewed.
