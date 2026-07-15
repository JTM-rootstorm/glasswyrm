#include "glasswyrmd/real_input_controller.hpp"

#include "input/fake_libinput_api.hpp"
#include "protocol/x11/event_mask.hpp"
#include "tests/helpers/test_support.hpp"

#include <linux/input-event-codes.h>

#include <memory>
#include <string>

namespace input = glasswyrm::input;
namespace server = glasswyrm::server;
namespace state = gw::protocol::x11::state_mask;
using gw::test::require;

namespace {

input::LibinputEvent device(const input::LibinputEventKind kind,
                            const std::uint64_t id,
                            const std::uint8_t capabilities) {
  input::LibinputEvent event;
  event.kind = kind;
  event.device_id = id;
  event.capabilities = capabilities;
  return event;
}

input::LibinputEvent event(const input::LibinputEventKind kind,
                           const std::uint64_t id,
                           const std::uint64_t time_usec) {
  input::LibinputEvent result;
  result.kind = kind;
  result.device_id = id;
  result.time_usec = time_usec;
  return result;
}

} // namespace

int main() {
  auto api = std::make_unique<input::FakeLibinputApi>();
  auto *fixture = api.get();
  fixture->queue(device(input::LibinputEventKind::DeviceAdded, 1,
                        input::DeviceCapabilityKeyboard));
  fixture->queue(device(input::LibinputEventKind::DeviceAdded, 2,
                        input::DeviceCapabilityPointer));
  server::RealInputControllerConfig config;
  config.device_paths = {"/dev/null"};
  config.root_width = 100;
  config.root_height = 80;
  std::string error;
  auto controller = server::RealInputController::create(
      std::move(api), std::move(config), error);
  require(controller != nullptr && controller->ready() &&
              controller->input_fd() == 73 && controller->repeat_fd() >= 0,
          error);

  auto motion = event(input::LibinputEventKind::MotionRelative, 2, 1000);
  motion.x = 4.0;
  motion.y = 3.0;
  fixture->queue(motion);
  auto shift = event(input::LibinputEventKind::Key, 1, 2000);
  shift.code = KEY_LEFTSHIFT;
  shift.pressed = true;
  fixture->queue(shift);
  auto key = event(input::LibinputEventKind::Key, 1, 3000);
  key.code = KEY_A;
  key.pressed = true;
  fixture->queue(key);
  auto button = event(input::LibinputEventKind::Button, 2, 4000);
  button.code = BTN_LEFT;
  button.pressed = true;
  fixture->queue(button);

  const auto serviced = controller->service_backend(0x440001U);
  require(serviced.success && !serviced.input_unavailable &&
              controller->queued_event_count() == 4,
          "backend records enter the bounded server queue");
  const auto routed_motion = controller->take_event();
  const auto routed_shift = controller->take_event();
  const auto routed_key = controller->take_event();
  const auto routed_button = controller->take_event();
  require(routed_motion &&
              routed_motion->kind == server::RealInputEventKind::Motion &&
              routed_motion->root_x == 4 && routed_motion->root_y == 3,
          "motion retains converted root coordinates");
  require(routed_shift && routed_shift->detail == KEY_LEFTSHIFT + 8 &&
              routed_shift->focus_window == 0x440001U &&
              routed_shift->state_before == 0 &&
              routed_shift->state_after == state::Shift && routed_key &&
              routed_key->detail == KEY_A + 8 &&
              routed_key->state_before == state::Shift &&
              routed_key->state_after == state::Shift,
          "xkb transition state is frozen before and after each key");
  require(routed_button && routed_button->detail == 1 &&
              routed_button->state_before == state::Shift &&
              routed_button->state_after == (state::Shift | state::Button1),
          "pointer buttons compose with xkb modifier state");

  auto inactive = controller->apply_session_state(GWIPC_SESSION_INACTIVE);
  require(inactive.result == GWIPC_SESSION_STATE_ACCEPTED &&
              inactive.reset_server_state && !controller->active() &&
              fixture->suspend_count() == 1 && !controller->has_events(),
          "inactive session clears repeat, held state, and queued records");
  inactive = controller->apply_session_state(GWIPC_SESSION_INACTIVE);
  require(inactive.result == GWIPC_SESSION_STATE_ALREADY_APPLIED,
          "repeated inactive state is idempotent");

  fixture->queue(device(input::LibinputEventKind::DeviceAdded, 1,
                        input::DeviceCapabilityKeyboard));
  fixture->queue(device(input::LibinputEventKind::DeviceAdded, 2,
                        input::DeviceCapabilityAbsolutePointer));
  const auto active = controller->apply_session_state(GWIPC_SESSION_ACTIVE);
  require(active.result == GWIPC_SESSION_STATE_ACCEPTED &&
              controller->ready() && fixture->resume_count() == 1,
          "active session resumes and revalidates both capabilities");

  fixture->queue(device(input::LibinputEventKind::DeviceRemoved, 2, 0));
  const auto removed = controller->service_backend(1);
  const auto reset = controller->take_event();
  require(removed.success && removed.input_unavailable && reset &&
              reset->kind == server::RealInputEventKind::StateReset &&
              !controller->ready(),
          "required device loss clears provider state without a fatal result");

  require(controller->apply_session_state(GWIPC_SESSION_INACTIVE).result ==
              GWIPC_SESSION_STATE_ACCEPTED,
          "unavailable active backend can still suspend");
  fixture->fail_resume("fixture resume failure");
  const auto failed = controller->apply_session_state(GWIPC_SESSION_ACTIVE);
  require(failed.result == GWIPC_SESSION_STATE_FAILED && failed.fatal &&
              failed.error.find("fixture resume failure") != std::string::npos,
          "resume failure is fatal to the real-input profile");
  return 0;
}
