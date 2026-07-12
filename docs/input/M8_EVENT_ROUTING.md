# Milestone 8 Event Routing

## Hit testing and source windows

Pointer hit testing walks committed root children from top to bottom and picks
the first viewable, policy-visible, non-cleanup-pending direct-root
InputOutput window containing the clamped pointer. Root is the fallback.
Children, InputOnly windows, borders, and hidden or metadata-only surfaces are
not targets in M8.

Pointer events start at the pointer target. Keyboard events start at committed
GWM focus, falling back to root when focus is invalid. Root coordinates are
absolute. Event coordinates subtract a top-level window's committed origin;
keyboard event coordinates may therefore be negative. For root delivery,
`child` is the pointer target; direct top-level delivery uses zero.

## Selection and propagation

KeyPress, KeyRelease, ButtonPress, ButtonRelease, and MotionNotify walk from
their source toward root. At the first window with matching live selections,
the event is delivered to all matching clients and propagation stops. If no
selection matches, the supported do-not-propagate mask may discard it before
the walk continues. ButtonPress selection remains exclusive per window.

Motion matches PointerMotion, or ButtonMotion while any button is down, or a
pressed button's corresponding Button1Motion through Button5Motion bit.
PointerMotionHint alone does not select an event. M8 does not implement an
implicit button grab: release uses the window under the pointer at release.

EnterNotify, LeaveNotify, FocusIn, and FocusOut never propagate. They go to
every live client selecting the exact event-window mask.

## State, order, and crossing

Key and button packets carry state before their transition. Motion and
crossing carry current state. Every accepted motion emits MotionNotify, even
if clamping leaves the coordinate unchanged. A target change first emits
LeaveNotify, then EnterNotify, then MotionNotify.

Crossing uses `Normal` mode. Root-to-child detail is `Inferior` then
`Ancestor`; child-to-root is `Ancestor` then `Inferior`; sibling-to-sibling is
`Nonlinear` for both. No virtual intermediate events exist in this one-layer
profile. The focus bit is set for the focused event window, or for root when a
direct-root child is focused.

Map, unmap, configure, restack, destroy, cleanup, and override-redirect changes
recompute the target at the unchanged pointer and may emit Leave then Enter,
but not Motion. After an ordinary lifecycle commit, existing structural and
exposure events retain their order, followed by FocusOut/FocusIn and then
Leave/Enter.

All event packets use the recipient's byte order, zero padding, and that
recipient's last processed request sequence. Unsupported behavior includes
grabs, pointer queries and motion history, child/InputOnly hit testing,
PointerMotionHint semantics, virtual crossing events, and real devices.

