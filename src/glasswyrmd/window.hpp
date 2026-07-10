#pragma once

#include "glasswyrmd/property.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace glasswyrm::server {

enum class WindowClass : std::uint16_t {
  CopyFromParent = 0,
  InputOutput = 1,
  InputOnly = 2,
};

enum class MapState : std::uint8_t { Unmapped = 0 };

struct WindowAttributes {
  std::uint32_t background_pixmap{0};
  std::uint32_t background_pixel{0};
  std::uint32_t border_pixmap{0};
  std::uint32_t border_pixel{0};
  std::uint8_t bit_gravity{0};
  std::uint8_t window_gravity{0};
  std::uint8_t backing_store{0};
  std::uint32_t backing_planes{0xffffffff};
  std::uint32_t backing_pixel{0};
  bool override_redirect{false};
  bool save_under{false};
  std::uint32_t event_mask{0};
  std::uint32_t do_not_propagate_mask{0};
  std::uint32_t colormap{0};
  std::uint32_t cursor{0};
};

struct WindowResource {
  std::uint32_t parent{0};
  std::vector<std::uint32_t> children;
  std::int16_t x{0};
  std::int16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t border_width{0};
  std::uint8_t depth{0};
  WindowClass window_class{WindowClass::InputOutput};
  std::uint32_t visual{0};
  MapState map_state{MapState::Unmapped};
  WindowAttributes attributes;
  std::unordered_map<std::uint32_t, Property> properties;
};

struct WindowCreateSpec {
  std::uint32_t xid{0};
  std::uint32_t parent{0};
  std::int16_t x{0};
  std::int16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t border_width{0};
  std::uint8_t depth{0};
  WindowClass window_class{WindowClass::CopyFromParent};
  std::uint32_t visual{0};
  WindowAttributes attributes;
};

}  // namespace glasswyrm::server
