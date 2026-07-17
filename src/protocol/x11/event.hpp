#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstdint>
#include <array>
#include <variant>
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
  bool synthetic{false};
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

enum class PropertyNotifyState : std::uint8_t { NewValue = 0, Deleted = 1 };

struct PropertyNotifyEvent {
  std::uint32_t window{0};
  std::uint32_t atom{0};
  std::uint32_t time{0};
  PropertyNotifyState state{PropertyNotifyState::NewValue};
};

struct SelectionClearEvent {
  std::uint32_t time{0};
  std::uint32_t owner{0};
  std::uint32_t selection{0};
};

struct SelectionRequestEvent {
  std::uint32_t time{0};
  std::uint32_t owner{0};
  std::uint32_t requestor{0};
  std::uint32_t selection{0};
  std::uint32_t target{0};
  std::uint32_t property{0};
};

struct SelectionNotifyEvent {
  std::uint32_t time{0};
  std::uint32_t requestor{0};
  std::uint32_t selection{0};
  std::uint32_t target{0};
  std::uint32_t property{0};
  bool synthetic{false};
};

using ClientMessageData =
    std::variant<std::array<std::uint8_t, 20>,
                 std::array<std::uint16_t, 10>,
                 std::array<std::uint32_t, 5>>;

struct ClientMessageEvent {
  std::uint32_t window{0};
  std::uint32_t type{0};
  ClientMessageData data{std::array<std::uint8_t, 20>{}};
  bool synthetic{false};
};

[[nodiscard]] std::vector<std::uint8_t> encode_destroy_notify(
    ByteOrder order, std::uint64_t sequence, const DestroyNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_unmap_notify(
    ByteOrder order, std::uint64_t sequence, const UnmapNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_map_notify(
    ByteOrder order, std::uint64_t sequence, const MapNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_configure_notify(
    ByteOrder order, std::uint64_t sequence, const ConfigureNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_property_notify(
    ByteOrder order, std::uint64_t sequence, const PropertyNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_selection_clear(
    ByteOrder order, std::uint64_t sequence, const SelectionClearEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_selection_request(
    ByteOrder order, std::uint64_t sequence,
    const SelectionRequestEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_selection_notify(
    ByteOrder order, std::uint64_t sequence, const SelectionNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_client_message(
    ByteOrder order, std::uint64_t sequence, const ClientMessageEvent& event);

}  // namespace gw::protocol::x11
