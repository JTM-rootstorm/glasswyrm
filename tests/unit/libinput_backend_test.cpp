#include "input/fake_libinput_api.hpp"
#include "input/libinput_backend.hpp"
#include "tests/helpers/test_support.hpp"

#include <linux/input-event-codes.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace input = glasswyrm::input;

namespace {

input::LibinputEvent device(input::LibinputEventKind kind, std::uint64_t id,
                            std::uint8_t capabilities) {
  input::LibinputEvent event;
  event.kind = kind;
  event.device_id = id;
  event.capabilities = capabilities;
  return event;
}

input::LibinputEvent event(input::LibinputEventKind kind, std::uint64_t id,
                           std::uint64_t usec) {
  input::LibinputEvent value;
  value.kind = kind;
  value.device_id = id;
  value.time_usec = usec;
  return value;
}

}  // namespace

int main() {
  using gw::test::require;
  input::LibinputTimestampConverter timestamps;
  require(timestamps.convert(0) == 1 && timestamps.convert(999) == 1 &&
              timestamps.convert(2000) == 2 && timestamps.convert(1000) == 2,
          "timestamps are nonzero and monotonic for stale input");
  timestamps.reset();
  const auto before_wrap =
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) *
      1000U;
  require(timestamps.convert(before_wrap) ==
              std::numeric_limits<std::uint32_t>::max() &&
              timestamps.convert(before_wrap + 1000U) == 1,
          "timestamps preserve natural 32-bit wrap while reserving zero");

  input::FakeLibinputApi api;
  api.queue(device(input::LibinputEventKind::DeviceAdded, 1,
                   input::DeviceCapabilityKeyboard));
  api.queue(device(input::LibinputEventKind::DeviceAdded, 2,
                   input::DeviceCapabilityPointer));
  input::LibinputBackend backend(api);
  std::string error;
  const std::vector<std::string> paths{"/dev/null"};
  require(backend.initialize(paths, 100, 80, error) &&
              backend.readiness().ready() && backend.poll_fd() == 73 &&
              api.consumed_event_count() == 2,
          "initial dispatch requires keyboard and pointer readiness");

  auto relative = event(input::LibinputEventKind::MotionRelative, 2, 1000);
  relative.x = 0.4;
  relative.y = 0.4;
  api.queue(relative);
  relative.time_usec = 2000;
  relative.x = 0.7;
  relative.y = 0.7;
  api.queue(relative);
  auto absolute = event(input::LibinputEventKind::MotionAbsolute, 2, 3000);
  absolute.x = 500;
  absolute.y = -10;
  api.queue(absolute);
  auto button = event(input::LibinputEventKind::Button, 2, 4000);
  button.code = BTN_LEFT;
  button.pressed = true;
  api.queue(button);
  button.time_usec = 5000;
  button.pressed = false;
  api.queue(button);
  button.time_usec = 6000;
  button.code = BTN_FORWARD;
  api.queue(button);
  auto wheel = event(input::LibinputEventKind::Wheel, 2, 7000);
  wheel.wheel_v120 = -60;
  api.queue(wheel);
  wheel.time_usec = 8000;
  api.queue(wheel);
  wheel.time_usec = 9000;
  wheel.scroll_axis = input::ScrollAxis::Horizontal;
  wheel.wheel_v120 = 120;
  api.queue(wheel);
  wheel.time_usec = 10000;
  wheel.scroll_axis = input::ScrollAxis::Vertical;
  api.queue(wheel);
  wheel.time_usec = 11000;
  wheel.scroll_axis = input::ScrollAxis::Horizontal;
  wheel.wheel_v120 = -120;
  api.queue(wheel);
  const auto converted = backend.service();
  require(converted.status == input::InputServiceStatus::Complete &&
              converted.consumed_events == 11 && converted.ignored_events == 1 &&
              converted.records.size() == 12,
          "motion, buttons, wheel detents, and ignored buttons are bounded");
  require(converted.records[0].kind == input::RealInputKind::MotionRelative &&
              converted.records[0].root_x == 1 &&
              converted.records[0].root_y == 1 &&
              converted.records[1].kind == input::RealInputKind::MotionAbsolute &&
              converted.records[1].root_x == 99 &&
              converted.records[1].root_y == 0,
          "subpixel relative motion accumulates and absolute motion clamps");
  require(converted.records[2].code == 1 && converted.records[2].pressed &&
              !converted.records[3].pressed &&
              converted.records[4].code == 4 && converted.records[4].pressed &&
              converted.records[5].code == 4 && !converted.records[5].pressed &&
              converted.records[6].code == 7 && converted.records[6].pressed &&
              converted.records[7].code == 7 && !converted.records[7].pressed &&
              converted.records[8].code == 5 && converted.records[8].pressed &&
              converted.records[9].code == 5 && !converted.records[9].pressed &&
              converted.records[10].code == 6 && converted.records[10].pressed &&
              converted.records[11].code == 6 && !converted.records[11].pressed,
          "evdev buttons and signed vertical/horizontal detents map to X buttons");

  std::vector<std::uint32_t> extra_button_codes;
  for (const auto code : {BTN_MIDDLE, BTN_RIGHT, BTN_EXTRA}) {
    button = event(input::LibinputEventKind::Button, 2, 11500);
    button.code = code;
    button.pressed = true;
    api.queue(button);
    button.pressed = false;
    api.queue(button);
  }
  const auto extra_buttons = backend.service();
  for (const auto& record : extra_buttons.records)
    if (record.pressed) extra_button_codes.push_back(record.code);
  require(extra_button_codes == std::vector<std::uint32_t>{2, 3, 9},
          "middle, right, and extra evdev buttons use their core mappings");

  backend.warp_pointer(20, 30);
  relative = event(input::LibinputEventKind::MotionRelative, 2, 11600);
  relative.x = 0.9;
  relative.y = 0.9;
  api.queue(relative);
  relative.time_usec = 11700;
  relative.x = 0.2;
  relative.y = 0.2;
  api.queue(relative);
  const auto after_warp = backend.service();
  require(after_warp.records.size() == 1 &&
              after_warp.records[0].root_x == 21 &&
              after_warp.records[0].root_y == 31,
          "pointer warp resets the relative-motion origin and fraction");

  auto key = event(input::LibinputEventKind::Key, 1, 12000);
  key.code = KEY_A;
  key.pressed = true;
  api.queue(key);
  button = event(input::LibinputEventKind::Button, 2, 13000);
  button.code = BTN_SIDE;
  button.pressed = true;
  api.queue(button);
  require(backend.service().records.size() == 2 &&
              backend.held_key_count() == 1 &&
              backend.held_button_count() == 1,
          "held state is tracked per provider device");
  require(backend.suspend(error) && !backend.active() &&
              backend.held_key_count() == 0 &&
              backend.held_button_count() == 0 &&
              backend.service().status == input::InputServiceStatus::Inactive,
          "suspend clears held state without publishing releases");

  api.queue(device(input::LibinputEventKind::DeviceAdded, 1,
                   input::DeviceCapabilityKeyboard));
  api.queue(device(input::LibinputEventKind::DeviceAdded, 2,
                   input::DeviceCapabilityAbsolutePointer));
  require(backend.resume(error) && backend.active() &&
              backend.readiness().ready() && api.suspend_count() == 1 &&
              api.resume_count() == 1,
          "resume rediscovers required capabilities including absolute pointer");
  api.queue(device(input::LibinputEventKind::DeviceRemoved, 1, 0));
  const auto removal = backend.service();
  require(removal.status == input::InputServiceStatus::Complete &&
              !backend.readiness().ready() && removal.ignored_events == 1 &&
              removal.provider_state_reset,
          "required-device removal marks input unavailable without failure");

  api.queue(event(input::LibinputEventKind::Unsupported, 2, 14000));
  api.queue(event(input::LibinputEventKind::Unsupported, 2, 15000));
  const auto bounded = backend.service({1, 1});
  require(bounded.status == input::InputServiceStatus::BudgetExhausted &&
              bounded.consumed_events == 1 && api.consumed_event_count() == 27,
          "event and work budgets preserve reactor fairness");

  input::FakeLibinputApi dispatch_failure_api;
  dispatch_failure_api.queue(device(input::LibinputEventKind::DeviceAdded, 1,
                                    input::DeviceCapabilityKeyboard));
  dispatch_failure_api.queue(device(input::LibinputEventKind::DeviceAdded, 2,
                                    input::DeviceCapabilityPointer));
  input::LibinputBackend dispatch_failure(dispatch_failure_api);
  require(dispatch_failure.initialize(paths, 10, 10, error),
          "failure-injection backend initializes");
  dispatch_failure_api.fail_dispatch("fixture dispatch failure");
  require(dispatch_failure.service().status == input::InputServiceStatus::Fatal,
          "dispatch failure is reported as fatal");

  input::FakeLibinputApi missing_pointer_api;
  missing_pointer_api.queue(device(input::LibinputEventKind::DeviceAdded, 1,
                                   input::DeviceCapabilityKeyboard));
  input::LibinputBackend missing_pointer(missing_pointer_api);
  require(!missing_pointer.initialize(paths, 10, 10, error) &&
              error.find("keyboard and pointer") != std::string::npos,
          "startup fails without both required capabilities");

  input::FakeLibinputApi resume_failure_api;
  resume_failure_api.queue(device(input::LibinputEventKind::DeviceAdded, 1,
                                  input::DeviceCapabilityKeyboard));
  resume_failure_api.queue(device(input::LibinputEventKind::DeviceAdded, 2,
                                  input::DeviceCapabilityPointer));
  input::LibinputBackend resume_failure(resume_failure_api);
  require(resume_failure.initialize(paths, 10, 10, error) &&
              resume_failure.suspend(error),
          "resume-failure backend suspends");
  resume_failure_api.fail_resume("fixture resume failure");
  require(!resume_failure.resume(error) &&
              error.find("fixture resume failure") != std::string::npos,
          "resume failure is returned to the caller");
}
