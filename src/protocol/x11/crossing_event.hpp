#pragma once

#include "protocol/x11/input_event.hpp"

namespace gw::protocol::x11 {

enum class NotifyDetail : std::uint8_t {
  Ancestor = 0, Virtual = 1, Inferior = 2, Nonlinear = 3,
  NonlinearVirtual = 4, Pointer = 5, PointerRoot = 6, None = 7,
};
enum class NotifyMode : std::uint8_t { Normal = 0, Grab = 1, Ungrab = 2, WhileGrabbed = 3 };

struct CrossingEvent {
  CoreEventType type{CoreEventType::EnterNotify};
  NotifyDetail detail{NotifyDetail::Ancestor};
  std::uint32_t time{0}, root{0}, event{0}, child{0};
  std::int16_t root_x{0}, root_y{0}, event_x{0}, event_y{0};
  std::uint16_t state{0};
  NotifyMode mode{NotifyMode::Normal};
  bool same_screen{true};
  bool focus{false};
};

[[nodiscard]] std::vector<std::uint8_t> encode_crossing_event(
    ByteOrder order, std::uint64_t sequence, const CrossingEvent& event);

}  // namespace gw::protocol::x11
