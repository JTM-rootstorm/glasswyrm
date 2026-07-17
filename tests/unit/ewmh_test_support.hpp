#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ewmh_test {

namespace x11 = gw::protocol::x11;

inline void create_window(glasswyrm::server::ServerState& state,
                          const glasswyrm::server::ClientId owner,
                          const std::uint32_t xid) {
  using namespace glasswyrm::server;
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 200;
  spec.height = 120;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  gw::test::require(
      state.resources().create_window(owner, 0x400000, 0x1fffff, spec) ==
          CreateWindowStatus::Success,
      "create EWMH message window");
}

inline x11::FramedRequest client_message(
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

inline const std::vector<std::uint32_t>& property_values(
    const glasswyrm::server::ServerState& state, const std::uint32_t window,
    const std::uint32_t property) {
  const auto* resource = state.resources().find_window(window);
  gw::test::require(resource && resource->properties.contains(property),
                    "read EWMH property values");
  const auto* values = std::get_if<std::vector<std::uint32_t>>(
      &resource->properties.at(property).data);
  gw::test::require(values != nullptr, "EWMH property uses 32-bit values");
  return *values;
}

}  // namespace ewmh_test
