#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 200;
  spec.height = 120;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create EWMH message window");
}

x11::FramedRequest client_message(
    const x11::ByteOrder order, const std::uint32_t root,
    const std::uint32_t window, const std::uint32_t type,
    const std::array<std::uint32_t, 5>& data) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(x11::CoreOpcode::SendEvent));
  writer.write_u8(0);
  writer.write_u16(11);
  writer.write_u32(root);
  writer.write_u32(0x00180000);
  writer.write_u8(static_cast<std::uint8_t>(x11::CoreEventType::ClientMessage));
  writer.write_u8(32);
  writer.write_u16(0);
  writer.write_u32(window);
  writer.write_u32(type);
  for (const auto value : data) writer.write_u32(value);
  x11::FramedRequest request;
  request.opcode = static_cast<std::uint8_t>(x11::CoreOpcode::SendEvent);
  request.bytes = std::move(writer).take();
  request.length_units = 11;
  return request;
}

void test_messages(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t window = 0x400010;
  create_window(state, 7, window);
  DispatchContext context{1, 0x400000, 0x1fffff, 31, order, true};
  const auto net_state = state.atoms().find("_NET_WM_STATE").value();
  const auto fullscreen =
      state.atoms().find("_NET_WM_STATE_FULLSCREEN").value();
  const auto above = state.atoms().find("_NET_WM_STATE_ABOVE").value();
  auto result = dispatch_request(
      state, context,
      client_message(order, state.screen().root_window, window, net_state,
                     {1, fullscreen, above, 1, 0}));
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_policy && result.deferred_policy->property &&
              result.deferred_policy->window.fullscreen_requested &&
              result.deferred_policy->window.above_requested &&
              !state.resources().find_window(window)->fullscreen_requested,
          "_NET_WM_STATE Add stages two known state atoms atomically");

  result = dispatch_request(
      state, context,
      client_message(order, state.screen().root_window, window, net_state,
                     {3, fullscreen, 0, 1, 0}));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "_NET_WM_STATE rejects unknown actions");

  const auto active = state.atoms().find("_NET_ACTIVE_WINDOW").value();
  result = dispatch_request(
      state, context,
      client_message(order, state.screen().root_window, window, active,
                     {1, 100, 0, 0, 0}));
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_policy &&
              result.deferred_policy->request_focus,
          "_NET_ACTIVE_WINDOW requests lifecycle-coordinated focus");

  const auto protocols = state.atoms().find("WM_PROTOCOLS").value();
  const auto delete_window = state.atoms().find("WM_DELETE_WINDOW").value();
  require(state.resources().change_property(
              window, protocols, Property{4, std::vector<std::uint32_t>{delete_window}},
              PropertyMode::Replace) == PropertyMutationStatus::Success,
          "install WM_DELETE_WINDOW protocol");
  const auto close = state.atoms().find("_NET_CLOSE_WINDOW").value();
  result = dispatch_request(
      state, context,
      client_message(order, state.screen().root_window, window, close,
                     {555, 1, 0, 0, 0}));
  const auto* notification = result.protocol_events.empty()
                                 ? nullptr
                                 : std::get_if<x11::ClientMessageEvent>(
                                       &result.protocol_events.front().event);
  const auto* data = notification
                         ? std::get_if<std::array<std::uint32_t, 5>>(
                               &notification->data)
                         : nullptr;
  require(notification && data &&
              result.protocol_events.front().delivery ==
                  ProtocolEventDelivery::WindowOwner &&
              notification->window == window && notification->type == protocols &&
              (*data)[0] == delete_window && (*data)[1] == 555,
          "_NET_CLOSE_WINDOW routes WM_DELETE_WINDOW to the target owner");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_messages(order);
  return 0;
}
