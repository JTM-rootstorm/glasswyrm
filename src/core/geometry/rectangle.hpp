#pragma once

#include <cstdint>
#include <optional>

namespace glasswyrm::geometry {

struct Rectangle {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
  friend bool operator==(const Rectangle&, const Rectangle&) = default;
};

[[nodiscard]] std::optional<Rectangle> intersect(Rectangle left,
                                                 Rectangle right) noexcept;

}  // namespace glasswyrm::geometry
