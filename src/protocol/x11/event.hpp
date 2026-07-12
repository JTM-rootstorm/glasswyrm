#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstdint>
#include <vector>

namespace gw::protocol::x11 {

struct DestroyNotifyEvent {
  std::uint32_t event{0};
  std::uint32_t window{0};
};

struct UnmapNotifyEvent {
  std::uint32_t event{0};
  std::uint32_t window{0};
  bool from_configure{false};
};

struct MapNotifyEvent {
  std::uint32_t event{0};
  std::uint32_t window{0};
  bool override_redirect{false};
};

struct ConfigureNotifyEvent {
  std::uint32_t event{0};
  std::uint32_t window{0};
  std::uint32_t above_sibling{0};
  std::int16_t x{0};
  std::int16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t border_width{0};
  bool override_redirect{false};
};

[[nodiscard]] std::vector<std::uint8_t> encode_destroy_notify(
    ByteOrder order, std::uint64_t sequence, const DestroyNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_unmap_notify(
    ByteOrder order, std::uint64_t sequence, const UnmapNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_map_notify(
    ByteOrder order, std::uint64_t sequence, const MapNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_configure_notify(
    ByteOrder order, std::uint64_t sequence, const ConfigureNotifyEvent& event);

}  // namespace gw::protocol::x11
