#pragma once

#include "protocol/x11/crossing_event.hpp"

namespace gw::protocol::x11 {
struct FocusEvent {
  CoreEventType type{CoreEventType::FocusIn};
  NotifyDetail detail{NotifyDetail::Ancestor};
  std::uint32_t event{0};
  NotifyMode mode{NotifyMode::Normal};
};
[[nodiscard]] std::vector<std::uint8_t> encode_focus_event(
    ByteOrder order, std::uint64_t sequence, const FocusEvent& event);
}  // namespace gw::protocol::x11
