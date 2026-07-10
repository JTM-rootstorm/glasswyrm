#pragma once

#include <cstdint>

namespace gw::protocol::x11 {

struct ScreenModel {
  std::uint32_t root_window{1};
  std::uint32_t default_colormap{2};
  std::uint32_t root_visual{3};
  std::uint8_t root_depth{24};
  std::uint16_t width_pixels{1024};
  std::uint16_t height_pixels{768};
  std::uint16_t width_millimeters{270};
  std::uint16_t height_millimeters{203};
  std::uint32_t red_mask{0x00ff0000};
  std::uint32_t green_mask{0x0000ff00};
  std::uint32_t blue_mask{0x000000ff};
  std::uint16_t maximum_request_length{65535};
  std::uint32_t resource_id_mask{0x001fffff};
};

inline constexpr ScreenModel kScreenModel{};

}  // namespace gw::protocol::x11
