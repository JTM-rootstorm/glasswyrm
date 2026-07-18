#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/event_mask.hpp"


using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

namespace {

x11::ByteWriter header(const x11::ByteOrder order,
                       const x11::CoreOpcode opcode, const std::uint8_t data,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer,
                          const x11::CoreOpcode opcode,
                          const std::uint8_t data = 0) {
  x11::FramedRequest request;
  request.opcode = static_cast<std::uint8_t>(opcode);
  request.data = data;
  request.bytes = std::move(writer).take();
  request.length_units =
      static_cast<std::uint16_t>(request.bytes.size() / 4U);
  return request;
}

WindowCreateSpec window(const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = 1;
  spec.width = 100;
  spec.height = 80;
  return spec;
}

void require_error(const DispatchResult& result, const x11::ByteOrder order,
                   const x11::CoreErrorCode code,
                   const std::uint32_t bad_value, const char* message) {
  x11::ByteReader reader(
      std::span<const std::uint8_t>(result.output).subspan(4), order);
  std::uint32_t wire_value{};
  require(result.output.size() == 32 && result.output[0] == 0 &&
              result.output[1] == static_cast<std::uint8_t>(code) &&
              reader.read_u32(wire_value) && wire_value == bad_value,
          message);
}

} // namespace

int main() {
  namespace mask = gw::protocol::x11::event_mask;
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    constexpr std::uint32_t base = 0x00400000;
    constexpr std::uint32_t resource_mask = 0x001fffff;
    const auto xid = base + 1;
    ServerState state;
    require(state.resources().create_window(7, base, resource_mask,
                                            window(xid)) ==
                CreateWindowStatus::Success,
            "create a viewable grab target");
    state.resources().find_window(xid)->map_requested = true;
    state.resources().find_window(xid)->policy_visible = true;
    state.resources().recompute_map_states(state.screen().root_window);
    DispatchContext context{7, base, resource_mask, 0x12345, order, false,
                            InputSnapshot{10, 20, 8, xid, 100}};
    context.input.keymap[8] = 1;

    auto result = dispatch_request(
        state, context,
        finish(header(order, x11::CoreOpcode::QueryKeymap, 0, 1),
               x11::CoreOpcode::QueryKeymap));
    require(result.output.size() == 40 && result.output[0] == 1 &&
                result.output[16] == 1,
            "QueryKeymap returns the live held-key bitmap");

    auto controls = header(order, x11::CoreOpcode::ChangeKeyboardControl, 0, 4);
    controls.write_u32((1U << 1U) | (1U << 3U));
    controls.write_u32(25);
    controls.write_u32(75);
    result = dispatch_request(
        state, context,
        finish(std::move(controls), x11::CoreOpcode::ChangeKeyboardControl));
    require(result.output.empty() &&
                state.keyboard_control().bell_percent == 25 &&
                state.keyboard_control().bell_duration == 75,
            "ChangeKeyboardControl applies the bounded bell subset");

    result = dispatch_request(
        state, context,
        finish(header(order, x11::CoreOpcode::GetKeyboardControl, 0, 1),
               x11::CoreOpcode::GetKeyboardControl));
    x11::ByteReader control_reply(
        std::span<const std::uint8_t>(result.output).subspan(8), order);
    std::uint32_t leds{};
    std::uint8_t click{}, bell_percent{};
    std::uint16_t pitch{}, duration{};
    require(result.output.size() == 52 && control_reply.read_u32(leds) &&
                control_reply.read_u8(click) &&
                control_reply.read_u8(bell_percent) &&
                control_reply.read_u16(pitch) &&
                control_reply.read_u16(duration) && bell_percent == 25 &&
                duration == 75,
            "GetKeyboardControl returns exact current state");

    result = dispatch_request(
        state, context,
        finish(header(order, x11::CoreOpcode::Bell, 100, 1),
               x11::CoreOpcode::Bell, 100));
    require(result.output.empty(), "Bell accepts the bounded audible range");

    result = dispatch_request(
        state, context,
        finish(header(order, x11::CoreOpcode::ForceScreenSaver, 0, 1),
               x11::CoreOpcode::ForceScreenSaver));
    require(result.output.empty(),
            "ForceScreenSaver reset is an accepted side-effect-free request");
    result = dispatch_request(
        state, context,
        finish(header(order, x11::CoreOpcode::ForceScreenSaver, 1, 1),
               x11::CoreOpcode::ForceScreenSaver, 1));
    require(result.output.empty(),
            "ForceScreenSaver active is an accepted side-effect-free request");
    require_error(
        dispatch_request(
            state, context,
            finish(header(order, x11::CoreOpcode::ForceScreenSaver, 2, 1),
                   x11::CoreOpcode::ForceScreenSaver, 2)),
        order, x11::CoreErrorCode::BadValue, 2,
        "ForceScreenSaver rejects an invalid mode");
    auto oversized_screen_saver =
        header(order, x11::CoreOpcode::ForceScreenSaver, 0, 2);
    oversized_screen_saver.write_u32(0);
    require_error(
        dispatch_request(
            state, context,
            finish(std::move(oversized_screen_saver),
                   x11::CoreOpcode::ForceScreenSaver)),
        order, x11::CoreErrorCode::BadLength, 0,
        "ForceScreenSaver rejects an invalid request length");

    const auto pointer_request = [&](const std::uint8_t owner_events,
                                     const std::uint8_t pointer_mode,
                                     const std::uint8_t keyboard_mode) {
      auto pointer =
          header(order, x11::CoreOpcode::GrabPointer, owner_events, 6);
      pointer.write_u32(xid);
      pointer.write_u16(mask::ButtonPress | mask::ButtonRelease |
                        mask::PointerMotion);
      pointer.write_u8(pointer_mode);
      pointer.write_u8(keyboard_mode);
      pointer.write_u32(0);
      pointer.write_u32(0);
      pointer.write_u32(0);
      return finish(std::move(pointer), x11::CoreOpcode::GrabPointer,
                    owner_events);
    };
    require_error(dispatch_request(state, context, pointer_request(2, 1, 1)),
                  order, x11::CoreErrorCode::BadValue, 2,
                  "GrabPointer rejects invalid owner-events as BadValue");
    require_error(dispatch_request(state, context, pointer_request(1, 2, 1)),
                  order, x11::CoreErrorCode::BadValue, 2,
                  "GrabPointer rejects pointer mode values above Async");
    require_error(dispatch_request(state, context, pointer_request(1, 1, 3)),
                  order, x11::CoreErrorCode::BadValue, 3,
                  "GrabPointer rejects keyboard mode values above Async");
    require_error(dispatch_request(state, context, pointer_request(1, 0, 1)),
                  order, x11::CoreErrorCode::BadImplementation, 0,
                  "GrabPointer reports valid synchronous mode as unsupported");
    result = dispatch_request(
        state, context, pointer_request(1, 1, 1));
    require(result.output.size() == 32 && result.output[0] == 1 &&
                result.output[1] == 0 && state.grabs().pointer_grab() &&
                state.grabs().pointer_grab()->client == 7,
            "GrabPointer parses in both byte orders and returns Success");

    auto change = header(order, x11::CoreOpcode::ChangeActivePointerGrab, 0, 4);
    change.write_u32(0);
    change.write_u32(0);
    change.write_u16(mask::ButtonRelease);
    change.write_u16(0);
    result = dispatch_request(
        state, context,
        finish(std::move(change), x11::CoreOpcode::ChangeActivePointerGrab));
    require(result.output.empty() &&
                state.grabs().pointer_grab()->event_mask ==
                    mask::ButtonRelease,
            "ChangeActivePointerGrab updates the active owner grab");

    auto ungrab = header(order, x11::CoreOpcode::UngrabPointer, 0, 2);
    ungrab.write_u32(0);
    result = dispatch_request(
        state, context,
        finish(std::move(ungrab), x11::CoreOpcode::UngrabPointer));
    require(result.output.empty() && !state.grabs().pointer_grab(),
            "UngrabPointer releases the active grab");

    auto passive = header(order, x11::CoreOpcode::GrabButton, 1, 6);
    passive.write_u32(xid);
    passive.write_u16(mask::ButtonPress | mask::ButtonRelease);
    passive.write_u8(1);
    passive.write_u8(1);
    passive.write_u32(0);
    passive.write_u32(0);
    passive.write_u8(1);
    passive.write_u8(0);
    passive.write_u16(8);
    result = dispatch_request(
        state, context,
        finish(std::move(passive), x11::CoreOpcode::GrabButton, 1));
    require(result.output.empty() && state.grabs().passive_button_count() == 1,
            "GrabButton installs an asynchronous passive grab");

    const auto keyboard_request = [&](const std::uint8_t owner_events,
                                      const std::uint8_t pointer_mode,
                                      const std::uint8_t keyboard_mode) {
      auto keyboard =
          header(order, x11::CoreOpcode::GrabKeyboard, owner_events, 4);
      keyboard.write_u32(xid);
      keyboard.write_u32(0);
      keyboard.write_u8(pointer_mode);
      keyboard.write_u8(keyboard_mode);
      keyboard.write_u16(0);
      return finish(std::move(keyboard), x11::CoreOpcode::GrabKeyboard,
                    owner_events);
    };
    require_error(dispatch_request(state, context, keyboard_request(2, 1, 1)),
                  order, x11::CoreErrorCode::BadValue, 2,
                  "GrabKeyboard rejects invalid owner-events as BadValue");
    require_error(dispatch_request(state, context, keyboard_request(0, 2, 1)),
                  order, x11::CoreErrorCode::BadValue, 2,
                  "GrabKeyboard rejects pointer mode values above Async");
    require_error(dispatch_request(state, context, keyboard_request(0, 1, 3)),
                  order, x11::CoreErrorCode::BadValue, 3,
                  "GrabKeyboard rejects keyboard mode values above Async");
    require_error(dispatch_request(state, context, keyboard_request(0, 1, 0)),
                  order, x11::CoreErrorCode::BadImplementation, 0,
                  "GrabKeyboard reports valid synchronous mode as unsupported");
    result = dispatch_request(
        state, context, keyboard_request(0, 1, 1));
    require(result.output.size() == 32 && result.output[1] == 0 &&
                state.grabs().keyboard_grab(),
            "GrabKeyboard returns Success for the asynchronous subset");

    auto allow = header(order, x11::CoreOpcode::AllowEvents, 3, 2);
    allow.write_u32(0);
    result = dispatch_request(
        state, context,
        finish(std::move(allow), x11::CoreOpcode::AllowEvents, 3));
    require(result.output.empty(), "AllowEvents accepts AsyncKeyboard");

    (void)state.cleanup_client(7);
    require(!state.grabs().keyboard_grab() &&
                state.grabs().passive_button_count() == 0,
            "client cleanup releases active and passive grabs");
  }
}
