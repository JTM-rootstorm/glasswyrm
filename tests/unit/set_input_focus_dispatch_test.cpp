#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

constexpr std::uint32_t kBase = 0x00400000;
constexpr std::uint32_t kMask = 0x001fffff;
constexpr std::uint32_t kFirst = kBase + 1;
constexpr std::uint32_t kSecond = kBase + 2;

x11::FramedRequest request(const x11::ByteOrder order,
                           const std::uint32_t focus,
                           const std::uint32_t time,
                           const std::uint8_t revert_to = 0) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(x11::CoreOpcode::SetInputFocus));
  writer.write_u8(revert_to);
  writer.write_u16(3);
  writer.write_u32(focus);
  writer.write_u32(time);
  x11::FramedRequest framed;
  framed.opcode =
      static_cast<std::uint8_t>(x11::CoreOpcode::SetInputFocus);
  framed.data = revert_to;
  framed.length_units = 3;
  framed.bytes = std::move(writer).take();
  return framed;
}

x11::FramedRequest short_request(const x11::ByteOrder order) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(x11::CoreOpcode::SetInputFocus));
  writer.write_u8(0);
  writer.write_u16(1);
  x11::FramedRequest framed;
  framed.opcode =
      static_cast<std::uint8_t>(x11::CoreOpcode::SetInputFocus);
  framed.length_units = 1;
  framed.bytes = std::move(writer).take();
  return framed;
}

WindowCreateSpec window(const std::uint32_t xid,
                        const std::uint32_t parent = 1) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = parent;
  spec.width = 160;
  spec.height = 100;
  spec.window_class = WindowClass::InputOutput;
  return spec;
}

ServerState state_with_windows() {
  ServerState state;
  require(state.resources().create_window(7, kBase, kMask, window(kFirst)) ==
              CreateWindowStatus::Success &&
              state.resources().create_window(7, kBase, kMask,
                                               window(kSecond)) ==
                  CreateWindowStatus::Success,
          "create focus targets");
  state.resources().find_window(kFirst)->map_requested = true;
  state.resources().find_window(kSecond)->map_requested = true;
  const std::array policy{
      AppliedPolicyWindow{kFirst, 10, 20, 160, 100, 0, true, false},
      AppliedPolicyWindow{kSecond, 30, 40, 160, 100, 1, true, false}};
  require(state.apply_policy(policy), "make focus targets viewable");
  return state;
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

void test_order(const x11::ByteOrder order) {
  auto state = state_with_windows();
  DispatchContext integrated{7, kBase, kMask, 0x12345, order, true,
                             InputSnapshot{10, 20, 0, kFirst, 100}};

  for (const auto special : {std::uint32_t{0}, std::uint32_t{1}}) {
    auto special_result =
        dispatch_request(state, integrated, request(order, special, 0));
    require(special_result.output.empty() &&
                special_result.kind == DispatchKind::Immediate &&
                state.focused_window() == state.screen().root_window,
            "None and PointerRoot are explicit root-focus no-ops");
  }

  auto result = dispatch_request(state, integrated,
                                 request(order, kFirst, 0, 2));
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_window == kFirst && result.deferred_policy &&
              result.deferred_policy->request_focus &&
              result.deferred_policy->window.xid == kFirst &&
              state.focused_window() == state.screen().root_window,
          "integrated SetInputFocus defers a GWM focus transaction");

  result = dispatch_request(state, integrated, request(order, kFirst, 101));
  require(result.kind == DispatchKind::Immediate && result.output.empty() &&
              !result.deferred_policy,
          "future SetInputFocus time is ignored");
  require_error(dispatch_request(state, integrated,
                                 request(order, kFirst, 0, 3)),
                order, x11::CoreErrorCode::BadValue, 3,
                "invalid revert-to is BadValue");
  require_error(dispatch_request(state, integrated,
                                 request(order, kBase + 99, 0)),
                order, x11::CoreErrorCode::BadWindow, kBase + 99,
                "missing focus target is BadWindow");
  require_error(dispatch_request(state, integrated, short_request(order)),
                order, x11::CoreErrorCode::BadLength, 0,
                "short SetInputFocus is BadLength");

  state.resources().find_window(kSecond)->map_requested = false;
  state.resources().recompute_map_states(state.screen().root_window);
  require_error(dispatch_request(state, integrated,
                                 request(order, kSecond, 0)),
                order, x11::CoreErrorCode::BadMatch, kSecond,
                "unviewable focus target is BadMatch");
  state.resources().find_window(kSecond)->map_requested = true;
  state.resources().recompute_map_states(state.screen().root_window);

  constexpr std::uint32_t child = kBase + 3;
  require(state.resources().create_window(7, kBase, kMask,
                                          window(child, kFirst)) ==
              CreateWindowStatus::Success,
          "create viewable child");
  state.resources().find_window(child)->map_requested = true;
  state.resources().recompute_map_states(state.screen().root_window);
  require_error(dispatch_request(state, integrated, request(order, child, 0)),
                order, x11::CoreErrorCode::BadImplementation, child,
                "non-policy focus target reports the unsupported boundary");

  DispatchContext local{7, kBase, kMask, 0x12346, order, false,
                        InputSnapshot{10, 20, 0, kFirst, 100}};
  result = dispatch_request(state, local, request(order, kFirst, 0));
  const auto* first = state.resources().find_window(kFirst);
  const auto* second = state.resources().find_window(kSecond);
  require(result.output.empty() && result.kind == DispatchKind::Immediate &&
              state.focused_window() == kFirst && first->focused &&
              !second->focused && first->focus_serial != 0,
          "local SetInputFocus commits exact focus state");
  for (const auto special : {std::uint32_t{0}, std::uint32_t{1}})
    require_error(dispatch_request(state, local,
                                   request(order, special, 0)),
                  order, x11::CoreErrorCode::BadImplementation, special,
                  "special focus transition reports unsupported GWM policy");
  const auto serial = first->focus_serial;
  result = dispatch_request(state, local, request(order, kFirst, 99, 1));
  require(result.output.empty() && first->focus_serial == serial,
          "already-focused target is an idempotent no-op");
}

}  // namespace

int main() {
  test_order(x11::ByteOrder::LittleEndian);
  test_order(x11::ByteOrder::BigEndian);
}
