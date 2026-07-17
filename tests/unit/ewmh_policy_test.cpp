#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t data = 0) {
  x11::FramedRequest request;
  request.opcode = 18;
  request.data = data;
  request.bytes = std::move(writer).take();
  request.length_units = request.bytes.size() / 4U;
  return request;
}

x11::FramedRequest change32(const x11::ByteOrder order,
                            const std::uint32_t window,
                            const std::uint32_t property,
                            const std::uint32_t type,
                            const std::vector<std::uint32_t>& values) {
  x11::ByteWriter writer(order);
  writer.write_u8(18);
  writer.write_u8(0);
  writer.write_u16(static_cast<std::uint16_t>(6 + values.size()));
  writer.write_u32(window);
  writer.write_u32(property);
  writer.write_u32(type);
  writer.write_u8(32);
  writer.write_padding(3);
  writer.write_u32(static_cast<std::uint32_t>(values.size()));
  for (const auto value : values) writer.write_u32(value);
  return finish(std::move(writer));
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t xid, const std::int16_t x = 10,
                   const std::int16_t y = 20) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.x = x;
  spec.y = y;
  spec.width = 320;
  spec.height = 240;
  spec.border_width = 2;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create EWMH policy window");
}

void test_atomic_fullscreen_and_restore(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t window = 0x400010;
  create_window(state, 1, window);
  DispatchContext integrated{1, 0x400000, 0x1fffff, 11, order, true};
  const auto state_atom = state.atoms().find("_NET_WM_STATE").value();
  const auto fullscreen =
      state.atoms().find("_NET_WM_STATE_FULLSCREEN").value();
  auto result = dispatch_request(
      state, integrated, change32(order, window, state_atom, 4, {fullscreen}));
  const auto* unchanged = state.resources().find_window(window);
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_policy && result.deferred_policy->property &&
              result.deferred_policy->window.fullscreen_requested &&
              result.deferred_policy->window.saved_normal_geometry &&
              !unchanged->fullscreen_requested &&
              !unchanged->properties.contains(state_atom),
          "policy property mutation remains staged until lifecycle acceptance");

  DispatchContext immediate = integrated;
  immediate.integrated_lifecycle = false;
  result = dispatch_request(
      state, immediate, change32(order, window, state_atom, 4, {fullscreen}));
  auto* applied = state.resources().find_window(window);
  require(result.output.empty() && applied->fullscreen_requested &&
              applied->saved_normal_geometry &&
              applied->saved_normal_geometry->x == 10 &&
              applied->saved_normal_geometry->width == 320,
          "entering fullscreen saves normal geometry exactly once");
  result = dispatch_request(state, immediate,
                            change32(order, window, state_atom, 4, {}));
  applied = state.resources().find_window(window);
  require(result.output.empty() && !applied->fullscreen_requested &&
              !applied->saved_normal_geometry && applied->requested_x == 10 &&
              applied->requested_y == 20 && applied->requested_width == 320 &&
              applied->requested_height == 240 &&
              applied->requested_border_width == 2,
          "leaving fullscreen restores the saved normal geometry");
}

void test_property_projection(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t parent = 0x400010;
  constexpr std::uint32_t window = 0x400011;
  create_window(state, 1, parent);
  create_window(state, 1, window, 30, 40);
  DispatchContext context{1, 0x400000, 0x1fffff, 12, order, false};

  const auto type_property = state.atoms().find("_NET_WM_WINDOW_TYPE").value();
  const auto utility = state.atoms().find("_NET_WM_WINDOW_TYPE_UTILITY").value();
  require(dispatch_request(state, context,
                           change32(order, window, type_property, 4, {utility}))
              .output.empty(),
          "window type property accepted");
  const auto motif = state.atoms().find("_MOTIF_WM_HINTS").value();
  require(dispatch_request(state, context,
                           change32(order, window, motif, motif,
                                    {2, 0, 0, 0, 0}))
              .output.empty(),
          "Motif decoration hints accepted");
  const auto bypass =
      state.atoms().find("_NET_WM_BYPASS_COMPOSITOR").value();
  require(dispatch_request(state, context,
                           change32(order, window, bypass, 6, {1}))
              .output.empty(),
          "bypass-compositor hint accepted");
  require(dispatch_request(state, context,
                           change32(order, window, 68, 33, {parent}))
              .output.empty(),
          "WM_TRANSIENT_FOR accepted");
  require(dispatch_request(
              state, context,
              change32(order, window, 40, 41,
                       {(1U << 4) | (1U << 5), 0, 0, 0, 0, 200, 100,
                        70000, 60000}))
              .output.empty(),
          "bounded WM_NORMAL_HINTS accepted");
  require(dispatch_request(state, context,
                           change32(order, window, 35, 35,
                                    {(1U << 0) | (1U << 8), 0}))
              .output.empty(),
          "WM_HINTS input and urgency accepted");

  const auto* projected = state.resources().find_window(window);
  require(projected->policy_window_type == PolicyWindowType::Utility &&
              projected->decoration_preference == PolicyDecoration::False &&
              projected->bypass_compositor &&
              projected->transient_for == parent &&
              projected->minimum_width == 200 &&
              projected->minimum_height == 100 &&
              projected->maximum_width == UINT16_MAX &&
              projected->maximum_height == 60000 &&
              !projected->input_requested && projected->attention_requested,
          "all bounded properties project into server policy metadata");

  const auto snapshot = state.lifecycle_snapshot();
  const auto& lifecycle = snapshot.windows.at(window);
  require(lifecycle.policy_window_type == PolicyWindowType::Utility &&
              lifecycle.decoration_preference == PolicyDecoration::False &&
              lifecycle.transient_for == parent && lifecycle.bypass_compositor &&
              lifecycle.minimum_width == 200 &&
              lifecycle.minimum_height == 100 &&
              lifecycle.maximum_width == UINT16_MAX &&
              lifecycle.maximum_height == 60000 &&
              !lifecycle.input_requested && lifecycle.attention_requested,
          "lifecycle snapshot carries GWM policy inputs");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_atomic_fullscreen_and_restore(order);
    test_property_projection(order);
  }
  return 0;
}
