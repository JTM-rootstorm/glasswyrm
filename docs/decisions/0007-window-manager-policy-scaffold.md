# 0007: Run Window-Management Policy as a Separate Service

Status: Accepted

## Context

Glasswyrm assigns X11 protocol truth to `glasswyrmd`, window-management policy
truth to `gwm`, and final presentation truth to `gwcomp`. Milestone 4 proved the
compositor boundary with synthetic surfaces, but `gwm` was still a placeholder.
The next boundary needed to be proven without coupling policy evaluation to the
X11 server, compositor, input, or display hardware.

## Decision

Milestone 5 makes `gwm` a foreground GWIPC service. It listens for one same-UID
peer with role `ProtocolServer` and requires the `Snapshots` and `WindowPolicy`
capabilities. The repository-owned `gwm_m5_producer` exercises that role; the
real `glasswyrmd` remains disconnected until a later milestone.

The producer supplies complete raw top-level window state. `gwm` owns separate
pending raw, committed raw, and committed policy states. Initial state and full
replacement use the `WindowPolicy` snapshot domain. Incremental full-record
upserts and removals are permitted only after a successful bootstrap. Policy is
evaluated only at a correlated `PolicyCommit`.

Accepted commits produce a complete output snapshot followed by a correlated
acknowledgement. Rejected commits produce only an acknowledgement and retain
the previously committed state. Disconnect clears all state and requires the
next peer to bootstrap again.

M5 deliberately supports one context, one workspace, and one logical work
area. Its policy is deterministic: ordinary windows use a 32-pixel cascade;
transients are centered and stacked over their parents; focus and stacking use
explicit serial tuples; override-redirect windows retain requested geometry and
remain unmanaged. The service computes decoration and fullscreen eligibility
but creates no decorations and makes no direct-scanout decision.

GWIPC wire 1.0 gains additive, capability-gated policy message IDs. The public
API advances to 0.3.0 while SOVERSION remains 0. Existing compositor contracts
and wire bytes are unchanged.

## Consequences

- Window policy is independently executable and testable without X11 mapping,
  a compositor, input devices, DRM/KMS, or a desktop session.
- Snapshot order, reconnect, focus, stacking, geometry, and policy hashes are
  deterministic and can be tested headlessly.
- One active ProtocolServer peer avoids premature multi-server namespace and
  merge semantics.
- Same-UID local IPC remains an early trust boundary, not process isolation.
- M5 does not connect the three runtime processes and establishes no claim of
  real X11 window-management compatibility.
