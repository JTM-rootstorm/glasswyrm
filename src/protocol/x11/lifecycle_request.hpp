#pragma once

#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/core.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace gw::protocol::x11 {

enum class LifecycleDecodeStatus { Complete, BadLength, BadValue, BadMatch };

struct WindowLifecycleRequest { std::uint32_t window{}; };

enum ConfigureValueMask : std::uint16_t {
  ConfigureX = 1U << 0, ConfigureY = 1U << 1,
  ConfigureWidth = 1U << 2, ConfigureHeight = 1U << 3,
  ConfigureBorderWidth = 1U << 4, ConfigureSibling = 1U << 5,
  ConfigureStackMode = 1U << 6,
};
inline constexpr std::uint16_t kKnownConfigureMask = UINT16_C(0x007f);

struct ConfigureWindowRequest {
  std::uint32_t window{};
  std::uint16_t value_mask{};
  std::optional<std::int32_t> x;
  std::optional<std::int32_t> y;
  std::optional<std::uint32_t> width;
  std::optional<std::uint32_t> height;
  std::optional<std::uint32_t> border_width;
  std::optional<std::uint32_t> sibling;
  std::optional<CoreStackMode> stack_mode;
};

[[nodiscard]] LifecycleDecodeStatus decode_map_window(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    WindowLifecycleRequest& request) noexcept;
[[nodiscard]] LifecycleDecodeStatus decode_map_subwindows(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    WindowLifecycleRequest& request) noexcept;
[[nodiscard]] LifecycleDecodeStatus decode_unmap_window(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    WindowLifecycleRequest& request) noexcept;
[[nodiscard]] LifecycleDecodeStatus decode_unmap_subwindows(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    WindowLifecycleRequest& request) noexcept;
[[nodiscard]] LifecycleDecodeStatus decode_configure_window(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    ConfigureWindowRequest& request) noexcept;

}  // namespace gw::protocol::x11
