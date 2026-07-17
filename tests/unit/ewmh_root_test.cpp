#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::FramedRequest request(x11::ByteWriter writer, const std::uint8_t opcode,
                           const std::uint8_t data) {
  x11::FramedRequest result;
  result.opcode = opcode;
  result.data = data;
  result.bytes = std::move(writer).take();
  result.length_units = result.bytes.size() / 4U;
  return result;
}

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t data, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

void create_window(ServerState& state, const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 100;
  spec.height = 80;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(1, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create EWMH root-list window");
}

void test_atom_and_root_profile(const x11::ByteOrder order) {
  ServerState historical;
  require(historical.atoms().intern("CUSTOM", false).atom == 69,
          "non-game dynamic atom numbering is unchanged");

  ServerState state(kScreenModel, true);
  require(state.game_compat() && state.atoms().find("_NET_SUPPORTED") == 69 &&
              state.atoms().find("UTF8_STRING") == 98 &&
              state.atoms().intern("CUSTOM", false).atom == 99,
          "game atoms are pre-interned once in documented order");
  const auto* proxy = state.resources().find_window(kEwmhSupportingWindow);
  require(proxy && proxy->parent == state.screen().root_window &&
              !state.resources().is_policy_candidate(kEwmhSupportingWindow),
          "supporting-WM proxy uses protected server ID 4");
  const auto supporting = state.atoms().find("_NET_SUPPORTING_WM_CHECK").value();
  const auto root_check = state.resources()
                              .find_window(state.screen().root_window)
                              ->properties.at(supporting);
  require(std::get<std::vector<std::uint32_t>>(root_check.data) ==
              std::vector<std::uint32_t>{kEwmhSupportingWindow},
          "root points to the supporting-WM proxy");
  const auto name_atom = state.atoms().find("_NET_WM_NAME").value();
  const auto& name = std::get<std::vector<std::uint8_t>>(
      proxy->properties.at(name_atom).data);
  require(std::string_view(reinterpret_cast<const char*>(name.data()),
                           name.size()) == "Glasswyrm",
          "supporting-WM proxy publishes a deterministic name");

  constexpr std::uint32_t window = 0x400010;
  create_window(state, window);
  synchronize_ewmh_root_properties(state);
  const auto clients_atom = state.atoms().find("_NET_CLIENT_LIST").value();
  require(std::get<std::vector<std::uint32_t>>(
              state.resources()
                  .find_window(state.screen().root_window)
                  ->properties.at(clients_atom)
                  .data) == std::vector<std::uint32_t>{window} &&
              state.invariants_hold(),
          "root client list excludes the server proxy and preserves invariants");

  const auto active = state.atoms().find("_NET_ACTIVE_WINDOW").value();
  auto writer = header(order, 18, 0, 7);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(active);
  writer.write_u32(33);
  writer.write_u8(32);
  writer.write_padding(3);
  writer.write_u32(1);
  writer.write_u32(window);
  DispatchContext context{1, 0x400000, 0x1fffff, 7, order};
  auto result = dispatch_request(state, context,
                                 request(std::move(writer), 18, 0));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess),
          "clients cannot replace protected root properties");

  writer = header(order, 19, 0, 3);
  writer.write_u32(kEwmhSupportingWindow);
  writer.write_u32(name_atom);
  result = dispatch_request(state, context,
                            request(std::move(writer), 19, 0));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess),
          "clients cannot delete supporting-proxy properties");

  writer = header(order, 4, 0, 2);
  writer.write_u32(kEwmhSupportingWindow);
  result = dispatch_request(state, context,
                            request(std::move(writer), 4, 0));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess),
          "clients cannot destroy the supporting-WM proxy");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_atom_and_root_profile(order);
  return 0;
}
