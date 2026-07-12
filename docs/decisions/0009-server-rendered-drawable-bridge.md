# 0009: Bridge Server-Rendered Drawables to the Compositor

Status: Accepted

## Context

The X11 core protocol assigns drawable resources, graphics contexts, and core
raster requests to the protocol server. Glasswyrm also keeps final scene
composition and presentation in `gwcomp`. Milestone 7 needs to connect those
responsibilities without letting `gwcomp` interpret X11 or allowing X11 writes
to race a compositor frame.

Milestone 6 already provides versioned GWIPC messages for memfd buffers,
surface damage, frame commits, acknowledgements, releases, snapshots, and
lifecycle metadata. A second transport would duplicate that contract and blur
the process boundary.

## Decision

`glasswyrmd` owns typed Pixmap and GraphicsContext resources and rasterizes the
documented core X11 subset into canonical XRGB8888 backing stores. Pixmaps are
server-local. A direct-root InputOutput window may be published to `gwcomp`,
which remains the only process that composes the final scene and writes frame
artifacts. `gwm` continues to own placement, stacking, visibility, and focus
policy and does not gain rendering work.

Each published top-level window uses two pixel stores:

- one mutable, heap-backed canonical store that is X11 protocol truth; and
- one persistent memfd-backed mirror synchronized for compositor consumption.

The compositor imports the mirror read-only. `glasswyrmd` copies canonical
damage to that mirror only when no compositor frame is using it. Drawing that
arrives while a frame is in flight continues to update canonical pixels and is
accumulated for a later frame.

The bridge is enabled only by explicit integrated-mode `--software-content`.
Integrated mode without the flag retains the Milestone 6 metadata-only
ProtocolServer path, including its no-PPM behavior. Standalone mode does not
publish content and rejects the flag.

GWIPC API 0.4.0, SOVERSION 0, and wire 1.0 remain unchanged. The buffered
ProtocolServer profile is selected from the existing peer role and capability
set. Complete lifecycle snapshots preserve known unchanged attachments;
creation, resize, and reconnect attach the required current buffers.

Only one compositor transaction is active at a time. Replay and bootstrap have
priority, followed by lifecycle commit or rollback, followed by incremental
content frames. Acknowledgement promotes submitted state. Buffer replacement
and surface removal retain old mappings until a matching `BufferRelease`, or
until compositor disconnect proves that the peer has dropped all mappings.

## Consequences

- X11 drawing remains synchronous protocol work even though presentation is
  asynchronous.
- Canonical pixels survive compositor rejection or restart and can be replayed
  deterministically.
- A stable mirror avoids descriptor and buffer-ID churn for ordinary damage.
- The reference path performs copies and full-map synchronization; it is not a
  performance or acceleration claim.
- Milestone 6 metadata-only evidence remains meaningful and opt-in Milestone 7
  behavior is easy to identify in tests and operations.

## Deferred scope

This decision does not add child-window composition, root or InputOnly drawing,
depth-1 drawables, `GetImage`, `CopyPlane`, text or fonts, tiles or stipples,
clip pixmaps, non-GXcopy functions, input, decorations, acceleration, DRM/KMS,
scaling, HDR, or VRR. Revisit the storage and synchronization strategy only
after the deterministic reference bridge has profiling evidence and broader
drawable semantics have an explicit compatibility target.
