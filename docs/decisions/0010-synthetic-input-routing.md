# 0010: Route Synthetic Input Through the Protocol Server

Status: Accepted

## Context

Milestone 8 needs deterministic input for headless protocol and compositor
tests. Input affects X11 event selection, propagation, focus, and client byte
order, so it must not become an independent policy authority or bypass the
protocol server.

## Decision

`glasswyrmd` owns pointer, button, key, modifier, hit-test, event-routing, and
X11 event-encoding state. A test-only provider connects to an opt-in GWIPC
listener as `DiagnosticTool` and sends device-like motion, button, key, and
barrier records. It never names an X11 target. The listener is not a fourth
runtime authority and no real-device backend is added in M8.

Button 1 may propose click focus, but `gwm` remains focus policy truth. The
proposal uses the existing lifecycle transaction and full policy snapshot;
`gwcomp` receives the committed projection only after policy acceptance.

M8 hit testing is intentionally one layer: visible, viewable, direct-root
InputOutput windows, in committed stack order. It excludes children,
InputOnly windows, and borders. The synthetic modifier map is fixed and does
not imply keysym, keyboard-map, lock, repeat, or XKB support. Grabs, including
the core automatic button grab, are not implemented.

The transport is enabled only by `--synthetic-input-socket`. Existing M1-M7
launches retain their behavior. GWIPC API 0.5 adds capability-gated messages
without changing SOVERSION 0 or wire version 1.0 and without changing an
existing message identifier or payload.

## Consequences

- A future libinput backend can feed the same internal router without GWIPC.
- Tests get bounded, credential-checked, correlated records and deterministic
  acknowledgements instead of an unversioned test socket.
- Focus ordering remains transactional across all three runtime processes.
- Compatibility is limited to the event-routing subset documented for M8.

