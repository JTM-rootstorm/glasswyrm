#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glasswyrm::drm {

enum class ConnectorType : std::uint32_t {
  Unknown = 0,
  Vga = 1,
  DviI = 2,
  DviD = 3,
  DviA = 4,
  Composite = 5,
  SVideo = 6,
  Lvds = 7,
  Component = 8,
  Din9 = 9,
  DisplayPort = 10,
  HdmiA = 11,
  HdmiB = 12,
  Tv = 13,
  Edp = 14,
  Virtual = 15,
  Dsi = 16,
  Dpi = 17,
  Writeback = 18,
  Spi = 19,
  Usb = 20,
};

enum class ConnectionStatus { Unknown, Connected, Disconnected };

struct Mode {
  std::string name;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t refresh_millihz{};
  std::uint32_t clock_khz{};
  bool preferred{false};
  std::uint16_t hsync_start{};
  std::uint16_t hsync_end{};
  std::uint16_t htotal{};
  std::uint16_t hskew{};
  std::uint16_t vsync_start{};
  std::uint16_t vsync_end{};
  std::uint16_t vtotal{};
  std::uint16_t vscan{};
  std::uint32_t flags{};
  std::uint32_t type{};
  std::uint32_t vrefresh_hz{};
};

struct Connector {
  std::uint32_t id{};
  std::uint32_t type{};
  std::uint32_t type_id{};
  ConnectionStatus status{ConnectionStatus::Unknown};
  std::vector<Mode> modes;
  bool non_desktop{false};
  std::uint32_t possible_crtc_mask{};
  std::uint32_t current_crtc_id{};
};

struct Crtc {
  std::uint32_t id{};
  std::uint32_t index{};
  std::vector<std::uint32_t> connector_ids;
  std::uint32_t framebuffer_id{};
  std::uint32_t x{};
  std::uint32_t y{};
  bool active{};
  Mode mode{};
};

enum class PlaneType { Unknown, Primary, Cursor, Overlay };

constexpr std::uint32_t fourcc(const char a, const char b, const char c,
                               const char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

inline constexpr std::uint32_t kFormatXrgb8888 = fourcc('X', 'R', '2', '4');

struct Plane {
  std::uint32_t id{};
  PlaneType type{PlaneType::Unknown};
  std::uint32_t possible_crtc_mask{};
  std::vector<std::uint32_t> formats;
  std::uint32_t current_crtc_id{};
  std::uint32_t framebuffer_id{};
  std::int32_t crtc_x{};
  std::int32_t crtc_y{};
  std::uint32_t crtc_width{};
  std::uint32_t crtc_height{};
  std::uint32_t source_x{};
  std::uint32_t source_y{};
  std::uint32_t source_width{};
  std::uint32_t source_height{};
};

} // namespace glasswyrm::drm
