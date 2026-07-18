# Milestone 12 XFIXES and DAMAGE Subsets

The `--game-compat` profile exposes a bounded XFIXES 2.0 selection/region
subset and a DAMAGE 1.1 tracking subset. These extensions share the canonical
server geometry and drawable-mutation paths; they do not create a parallel
shape or rendering model.

## XFIXES 2.0

`QueryVersion` negotiates no version newer than 2.0.
`SelectSelectionInput` accepts owner-change masks for `PRIMARY` and
`CLIPBOARD`. Glasswyrm records the selecting client, window, selection, and
mask, and emits `SelectionNotify` for supported ownership transitions:

- `SetSelectionOwner` when an owner is set, replaced, or cleared;
- `SelectionWindowDestroy` when a directly committed window destruction
  removes the owner; and
- `SelectionClientClose` during the ordinary non-integrated client cleanup
  path.

The event carries the subscription window, current owner or `None`, selection,
logical timestamp, selection-change timestamp, and the recipient's current
sequence. Event encoding uses the recipient's byte order. Selection
subscriptions are bounded to 4,096 tracked entries and are removed with the
selecting window or client.

The implemented region requests are:

- `CreateRegion` and `SetRegion`;
- `DestroyRegion` and `FetchRegion`;
- `CopyRegion`;
- `UnionRegion`, `IntersectRegion`, and `SubtractRegion`;
- `TranslateRegion`; and
- `RegionExtents`.

An XFixes region is a client-owned resource containing deterministic,
non-overlapping rectangles in stable coordinate order. Union, intersection,
and subtraction preserve exact covered area instead of replacing overlap with
an approximate bounding box. Empty rectangles are ignored. Translation checks
that every resulting origin remains representable by the protocol's signed
16-bit coordinates, and operations compute their complete result before
replacing a destination region.

Each client may own at most 4,096 regions, and a normalized region may contain
at most 4,096 rectangles. Region identifiers use the normal client resource-ID
rules. A missing region produces the extension-specific `BadRegion` error,
including the request major/minor metadata. Region resources are released on
`DestroyRegion` or client cleanup.

## DAMAGE 1.1

`QueryVersion` negotiates no version newer than 1.1. The implemented requests
are `Create`, `Destroy`, `Subtract`, and `Add`. A client may own at most 4,096
Damage resources.

`Create` accepts the `BoundingBox` and `NonEmpty` report levels for an existing
window or pixmap. `RawRectangles` and `DeltaRectangles` return `BadValue`
without partially creating a resource. A missing Damage identifier produces
the extension-specific `BadDamage` error. Region arguments to DAMAGE requests
remain XFIXES resources and therefore produce `BadRegion` when invalid.

Every Damage resource accumulates a normalized region for its selected
drawable. `BoundingBox` reports the current accumulated extents after each
accepted mutation. `NonEmpty` reports only the empty-to-nonempty transition;
it becomes eligible again after subtraction empties the accumulated region. If
damage-region complexity cannot remain within the rectangle bound, Glasswyrm
conservatively falls back to the drawable's complete geometry.

`Subtract` accepts `None` for the repair or parts region where the protocol
defines it. With no repair region, the complete accumulated region is copied
to the optional parts region and then cleared. With a repair region, the parts
region receives the damaged portion intersecting the repair, while the
repaired area is removed from accumulated damage. `Add` feeds every rectangle
from an XFIXES region through the same Damage accumulation and notification
path as a drawable mutation.

`DamageNotify` contains the report level, drawable, Damage identifier, logical
time, reported area, and complete drawable geometry. Delivery uses the
recipient's current sequence and byte order. Damage resources are released
when explicitly destroyed, when their owner disconnects, or automatically
when the watched window or pixmap is destroyed.

## Canonical damage source

External DAMAGE tracking is attached to the existing canonical mutation hook.
Covered operations include core software drawing, text, copy, `ClearArea`,
depth-1 and depth-24 `PutImage`, MIT-SHM `PutImage` through its core delegation,
and locally committed child-composition lifecycle changes. Window mutations
continue to feed internal top-level content publication from the same accepted
rectangles; pixmap mutations feed external Damage resources without pretending
that pixmaps are published outputs.

## Explicit deferrals

The Milestone 12 profile does not implement these XFIXES facilities:

- cursor image/name requests or cursor input selection;
- pointer barriers;
- cursor hiding and showing;
- regions created from bitmaps, windows, GCs, or pictures;
- window shape regions; or
- GC and picture clip-region requests.

DAMAGE `RawRectangles` and `DeltaRectangles` report levels are also deferred.
The extensions do not add new raster operations, output-management behavior,
or client isolation guarantees.

## Current proof boundary

The `xfixes-damage` unit/protocol test runs in both client byte orders. It
proves version negotiation, exact selection and damage event wire fields,
selection owner-change and direct window-destroy notifications, normalized
region union/intersection/subtraction/translation/fetch behavior, `BadRegion`,
canonical `PutImage` damage, `NonEmpty` suppression and repair reset,
`DamageSubtract`, `BadDamage`, watched-drawable destruction, and owner cleanup.
The MIT-SHM, protocol-foundation, resource-table, and source-layout suites run
alongside it.

This proof does not claim full XFIXES or DAMAGE compatibility. In particular,
the current test does not exercise every implemented region request or
`DamageAdd` over a live XCB connection. Deferred/integrated lifecycle commits
still need routing coverage for XFIXES owner-loss events and external damage
from deferred child-composition changes. The sandboxed test run also does not
provide a live Unix-socket process proof; that belongs to the fresh-VM SDL
acceptance path.
