#include "glasswyrmd/grab_state.hpp"

#include "helpers/test_support.hpp"
#include "protocol/x11/event_mask.hpp"

#include <cstdint>

using namespace glasswyrm::server;
using gw::test::require;
namespace em = gw::protocol::x11::event_mask;

namespace {

PointerGrabRequest pointer_request(const GrabClientId client = 1) {
  PointerGrabRequest request;
  request.client = client;
  request.window = 10;
  request.event_mask = em::ButtonPress | em::ButtonRelease | em::PointerMotion;
  request.current_time = 100;
  return request;
}

KeyboardGrabRequest keyboard_request(const GrabClientId client = 1) {
  KeyboardGrabRequest request;
  request.client = client;
  request.window = 10;
  request.current_time = 100;
  return request;
}

PassiveButtonGrabRequest passive_request(const GrabClientId client = 1) {
  PassiveButtonGrabRequest request;
  request.client = client;
  request.window = 10;
  request.button = 1;
  request.modifiers = 4;
  request.owner_events = false;
  request.event_mask = em::ButtonPress | em::ButtonRelease | em::PointerMotion;
  return request;
}

}  // namespace

int main() {
  GrabState grabs;
  require(grabs.begin_automatic_button_grab(1, 10, 1, 20) &&
              grabs.pointer_grab()->origin == PointerGrabOrigin::AutomaticButton &&
              grabs.pointer_grab()->owner_events,
          "delivered ButtonPress begins one owner-events automatic grab");
  auto route = grabs.route_pointer({2, 20, em::PointerMotion, 99});
  require(route.kind == GrabRouteKind::GrabWindow && route.client == 1 &&
              route.window == 10 && route.crossing_target == 99,
          "automatic grab redirects motion but preserves real crossing target");
  route = grabs.route_pointer({1, 11, em::PointerMotion, 99});
  require(route.kind == GrabRouteKind::Natural && route.window == 11,
          "automatic owner-events route stays natural for grabbing client");
  grabs.note_button_press(2);
  grabs.note_button_press(9);
  require(!grabs.note_button_release(1) && grabs.pointer_grab().has_value() &&
              !grabs.note_button_release(2) &&
              grabs.note_button_release(9) && !grabs.pointer_grab().has_value(),
          "automatic grab releases only after every held button is up");

  auto pointer = pointer_request();
  pointer.pointer_mode = GrabMode::Synchronous;
  require(grabs.grab_pointer(pointer) == GrabStatus::InvalidMode,
          "synchronous pointer mode is rejected");
  pointer = pointer_request();
  pointer.event_mask = em::KeyPress;
  require(grabs.grab_pointer(pointer) == GrabStatus::InvalidEventMask,
          "non-pointer event mask is rejected");
  pointer = pointer_request();
  pointer.confine_to = 12;
  require(grabs.grab_pointer(pointer) == GrabStatus::UnsupportedConfine,
          "only confine-to None is supported");
  pointer = pointer_request();
  pointer.cursor = 7;
  pointer.cursor_valid = false;
  require(grabs.grab_pointer(pointer) == GrabStatus::InvalidCursor,
          "optional grab cursor must be valid");
  pointer = pointer_request();
  pointer.window_viewable = false;
  require(grabs.grab_pointer(pointer) == GrabStatus::NotViewable,
          "grab window must be viewable");
  pointer = pointer_request();
  pointer.request_time = 101;
  require(grabs.grab_pointer(pointer) == GrabStatus::InvalidTime,
          "future grab time is rejected");

  pointer = pointer_request();
  pointer.owner_events = false;
  pointer.cursor = 7;
  require(grabs.grab_pointer(pointer) == GrabStatus::Success &&
              grabs.grab_pointer(pointer_request(2)) == GrabStatus::AlreadyGrabbed,
          "one active pointer grab returns AlreadyGrabbed to another request");
  route = grabs.route_pointer({1, 11, em::PointerMotion, 77});
  require(route.kind == GrabRouteKind::GrabWindow && route.window == 10,
          "owner-events false always routes selected events to grab window");
  route = grabs.route_pointer({1, 11, em::EnterWindow, 77});
  require(route.kind == GrabRouteKind::Suppressed && route.crossing_target == 77,
          "events outside active mask are suppressed without losing target truth");
  require(grabs.change_active_pointer_grab(2, em::PointerMotion, 0, true, 0, 100) ==
              GrabStatus::NotOwner &&
              grabs.change_active_pointer_grab(1, em::PointerMotion, 9, false, 0, 100) ==
                  GrabStatus::InvalidCursor &&
              grabs.change_active_pointer_grab(1, em::PointerMotion, 0, true, 0, 100) ==
                  GrabStatus::Success &&
              grabs.pointer_grab()->event_mask == em::PointerMotion,
          "active pointer mask/cursor changes validate ownership atomically");
  require(!grabs.ungrab_pointer(2, 0, 100) &&
              !grabs.ungrab_pointer(1, 99, 100) &&
              grabs.ungrab_pointer(1, 0, 100),
          "ungrab requires owner and a valid timestamp");

  auto keyboard = keyboard_request();
  keyboard.owner_events = true;
  require(grabs.grab_keyboard(keyboard) == GrabStatus::Success &&
              grabs.grab_keyboard(keyboard_request(2)) == GrabStatus::AlreadyGrabbed,
          "one asynchronous keyboard grab is supported");
  route = grabs.route_keyboard({1, 11, em::KeyPress, 55});
  require(route.kind == GrabRouteKind::Natural && route.window == 11,
          "keyboard owner-events preserves natural same-client delivery");
  route = grabs.route_keyboard({2, 20, em::KeyPress, 55});
  require(route.kind == GrabRouteKind::GrabWindow && route.client == 1 &&
              route.window == 10,
          "keyboard grab redirects other-client delivery");
  require(grabs.allow_events(AllowEventsMode::AsyncPointer, 0, 100) ==
              GrabStatus::Success &&
              grabs.allow_events(AllowEventsMode::AsyncKeyboard, 0, 100) ==
                  GrabStatus::Success &&
              grabs.allow_events(AllowEventsMode::AsyncBoth, 0, 100) ==
                  GrabStatus::Success &&
              grabs.allow_events(AllowEventsMode::ReplayKeyboard, 0, 100) ==
                  GrabStatus::BadImplementation,
          "AllowEvents accepts exactly the asynchronous subset");
  require(grabs.ungrab_keyboard(1, 0, 100),
          "keyboard owner can ungrab with CurrentTime");

  auto passive = passive_request();
  passive.cursor = 3;
  require(grabs.grab_button(passive) == GrabStatus::Success &&
              grabs.passive_button_count() == 1,
          "Ctrl+Button1-style passive grab installs");
  auto replacement = passive;
  replacement.event_mask = em::ButtonPress | em::ButtonRelease;
  require(grabs.grab_button(replacement) == GrabStatus::Success &&
              grabs.passive_button_count() == 1,
          "same client exact passive grab is replaced deterministically");
  auto conflicting = passive_request(2);
  require(grabs.grab_button(conflicting) == GrabStatus::BadAccess,
          "overlapping passive grab by another client returns BadAccess");
  auto any = passive_request();
  any.button = kAnyButton;
  any.modifiers = kAnyModifier;
  any.window = 20;
  any.cursor = 0;
  require(grabs.grab_button(any) == GrabStatus::Success &&
              grabs.activate_passive_button(3, 0x13, 120) &&
              grabs.pointer_grab()->origin == PointerGrabOrigin::PassiveButton &&
              grabs.pointer_grab()->client == 1 &&
              grabs.pointer_grab()->window == 20,
          "AnyButton/AnyModifier passive grab activates on normalized modifiers");
  require(grabs.note_button_release(3) && !grabs.pointer_grab().has_value(),
          "activated passive grab releases when buttons are up");
  require(grabs.activate_passive_button(1, 4, 121) &&
              grabs.pointer_grab()->window == 10,
          "specific xterm-style Ctrl+Button grab activates");
  require(grabs.suspend().pointer_released &&
              grabs.passive_button_count() == 2,
          "VT suspend cancels active grab but retains passive registrations");
  require(grabs.ungrab_button(1, 20, kAnyButton, kAnyModifier) == 1 &&
              grabs.passive_button_count() == 1,
          "UngrabButton wildcard removes matching owned registrations");

  require(grabs.grab_keyboard(keyboard_request(7)) == GrabStatus::Success,
          "cleanup keyboard fixture");
  auto owned = passive_request(7);
  owned.window = 30;
  require(grabs.grab_button(owned) == GrabStatus::Success,
          "cleanup passive fixture");
  auto cleanup = grabs.cleanup_client(7);
  require(cleanup.keyboard_released && cleanup.passive_buttons_removed == 1 &&
              !grabs.keyboard_grab().has_value(),
          "client disconnect releases active and passive grabs");
  cleanup = grabs.cleanup_window(10);
  require(cleanup.passive_buttons_removed == 1 &&
              grabs.passive_button_count() == 0,
          "window destruction removes its passive grabs");

  GrabState owner_events;
  pointer = pointer_request();
  pointer.owner_events = true;
  require(owner_events.grab_pointer(pointer) == GrabStatus::Success,
          "owner-events fixture");
  route = owner_events.route_pointer({1, 12, em::PointerMotion, 12});
  require(route.kind == GrabRouteKind::Natural && route.window == 12,
          "owner-events true preserves natural route to grabbing client");
  route = owner_events.route_pointer({2, 13, em::PointerMotion, 13});
  require(route.kind == GrabRouteKind::GrabWindow && route.window == 10,
          "owner-events true redirects routes belonging to another client");
}
