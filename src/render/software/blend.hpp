#pragma once

#include "render/software/pixel.hpp"

#include <cstdint>

namespace gw::render::software {

inline constexpr std::uint32_t full_opacity = 0x00010000U;

[[nodiscard]] Pixel apply_opacity(Pixel source,
                                  std::uint32_t opacity) noexcept;
[[nodiscard]] Pixel source_over(Pixel source, Pixel destination) noexcept;
[[nodiscard]] Pixel blend(Pixel source, Pixel destination,
                          std::uint32_t opacity) noexcept;

} // namespace gw::render::software
