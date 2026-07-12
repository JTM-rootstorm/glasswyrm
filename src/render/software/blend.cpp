#include "render/software/blend.hpp"

#include <algorithm>

namespace gw::render::software {
namespace {

[[nodiscard]] std::uint8_t scale(const std::uint8_t value,
                                 const std::uint32_t opacity) noexcept {
  const std::uint32_t bounded = std::min(opacity, full_opacity);
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(value) * bounded + 32768U) >> 16);
}

[[nodiscard]] std::uint8_t over_channel(const std::uint8_t source,
                                        const std::uint8_t destination,
                                        const std::uint8_t alpha) noexcept {
  const std::uint32_t result =
      source + (static_cast<std::uint32_t>(destination) * (255U - alpha) + 127U) /
                   255U;
  return static_cast<std::uint8_t>(std::min(result, 255U));
}

} // namespace

Pixel apply_opacity(Pixel source, const std::uint32_t opacity) noexcept {
  source.red = scale(source.red, opacity);
  source.green = scale(source.green, opacity);
  source.blue = scale(source.blue, opacity);
  source.alpha = scale(source.alpha, opacity);
  return source;
}

Pixel source_over(const Pixel source, const Pixel destination) noexcept {
  return Pixel{over_channel(source.red, destination.red, source.alpha),
               over_channel(source.green, destination.green, source.alpha),
               over_channel(source.blue, destination.blue, source.alpha), 255};
}

Pixel blend(const Pixel source, const Pixel destination,
            const std::uint32_t opacity) noexcept {
  return source_over(apply_opacity(source, opacity), destination);
}

} // namespace gw::render::software
