#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

inline constexpr std::size_t kMaximumXFixesRegionRectangles = 4096;

struct XFixesRegionResource {
  std::vector<geometry::Rectangle> rectangles;
};

[[nodiscard]] std::optional<std::vector<geometry::Rectangle>>
normalize_region(std::span<const geometry::Rectangle> rectangles);
[[nodiscard]] std::optional<std::vector<geometry::Rectangle>> union_regions(
    std::span<const geometry::Rectangle> left,
    std::span<const geometry::Rectangle> right);
[[nodiscard]] std::optional<std::vector<geometry::Rectangle>> intersect_regions(
    std::span<const geometry::Rectangle> left,
    std::span<const geometry::Rectangle> right);
[[nodiscard]] std::optional<std::vector<geometry::Rectangle>> subtract_regions(
    std::span<const geometry::Rectangle> left,
    std::span<const geometry::Rectangle> right);
[[nodiscard]] std::optional<std::vector<geometry::Rectangle>> translate_region(
    std::span<const geometry::Rectangle> source, std::int16_t dx,
    std::int16_t dy);
[[nodiscard]] geometry::Rectangle region_extents(
    std::span<const geometry::Rectangle> rectangles) noexcept;

}  // namespace glasswyrm::server
