#pragma once

#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/core.hpp"

#include <cstdint>
#include <vector>

namespace gw::protocol::x11 {

struct InputEvent {
  CoreEventType type{CoreEventType::MotionNotify};
  std::uint8_t detail{0};
  std::uint32_t time{0};
  std::uint32_t root{0};
  std::uint32_t event{0};
  std::uint32_t child{0};
  std::int16_t root_x{0};
  std::int16_t root_y{0};
  std::int16_t event_x{0};
  std::int16_t event_y{0};
  std::uint16_t state{0};
  bool same_screen{true};
};

[[nodiscard]] std::vector<std::uint8_t> encode_input_event(
    ByteOrder order, std::uint64_t sequence, const InputEvent& event);

}  // namespace gw::protocol::x11
