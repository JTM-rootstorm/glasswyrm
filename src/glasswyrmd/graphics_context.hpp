#pragma once
#include <cstdint>
namespace glasswyrm::server {
struct GraphicsContextResource {
  std::uint32_t root{};
  std::uint8_t depth{24};
  std::uint8_t function{3};
  std::uint32_t plane_mask{0xffffffffU};
  std::uint32_t foreground{};
  std::uint32_t background{1};
  std::uint8_t fill_style{};
  std::uint8_t subwindow_mode{};
  bool graphics_exposures{true};
  std::int16_t clip_x_origin{}, clip_y_origin{};
  std::uint32_t clip_mask{};
  std::uint32_t font{0xfffffff0U};
};
}  // namespace glasswyrm::server
