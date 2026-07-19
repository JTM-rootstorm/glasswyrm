#include "output/model/transform.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace glasswyrm::output {
namespace {

[[nodiscard]] bool contains_boundary(const PhysicalExtent extent,
                                     const PhysicalPoint point) noexcept {
  return point.x <= extent.width && point.y <= extent.height;
}

[[nodiscard]] bool
contains_rectangle(const PhysicalExtent extent,
                   const PhysicalRectangle rectangle) noexcept {
  return static_cast<std::uint64_t>(rectangle.x) + rectangle.width <=
             extent.width &&
         static_cast<std::uint64_t>(rectangle.y) + rectangle.height <=
             extent.height;
}

template <typename Mapper>
[[nodiscard]] std::optional<PhysicalRectangle>
map_rectangle_corners(const PhysicalRectangle rectangle,
                      Mapper &&mapper) noexcept {
  const auto x1 = static_cast<std::uint32_t>(rectangle.x + rectangle.width);
  const auto y1 = static_cast<std::uint32_t>(rectangle.y + rectangle.height);
  const std::array<PhysicalPoint, 4> corners{{
      {rectangle.x, rectangle.y},
      {x1, rectangle.y},
      {rectangle.x, y1},
      {x1, y1},
  }};
  std::array<PhysicalPoint, 4> mapped{};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    const auto point = mapper(corners[index]);
    if (!point) {
      return std::nullopt;
    }
    mapped[index] = *point;
  }
  const auto [minimum_x, maximum_x] = std::minmax_element(
      mapped.begin(), mapped.end(),
      [](const PhysicalPoint left, const PhysicalPoint right) {
        return left.x < right.x;
      });
  const auto [minimum_y, maximum_y] = std::minmax_element(
      mapped.begin(), mapped.end(),
      [](const PhysicalPoint left, const PhysicalPoint right) {
        return left.y < right.y;
      });
  return PhysicalRectangle{minimum_x->x, minimum_y->y,
                           maximum_x->x - minimum_x->x,
                           maximum_y->y - minimum_y->y};
}

} // namespace

bool valid_output_transform(const OutputTransform transform) noexcept {
  return transform >= OutputTransform::Normal &&
         transform <= OutputTransform::Flipped270;
}

bool transform_swaps_axes(const OutputTransform transform) noexcept {
  return transform == OutputTransform::Rotate90 ||
         transform == OutputTransform::Rotate270 ||
         transform == OutputTransform::Flipped90 ||
         transform == OutputTransform::Flipped270;
}

PhysicalExtent
transformed_physical_extent(const PhysicalExtent native_extent,
                            const OutputTransform transform) noexcept {
  if (!valid_output_transform(transform)) {
    return {};
  }
  if (transform_swaps_axes(transform)) {
    return PhysicalExtent{native_extent.height, native_extent.width};
  }
  return native_extent;
}

std::optional<PhysicalPoint>
transform_boundary(const PhysicalPoint transformed_point,
                   const PhysicalExtent native_extent,
                   const OutputTransform transform) noexcept {
  if (!valid_output_transform(transform) ||
      !contains_boundary(transformed_physical_extent(native_extent, transform),
                         transformed_point)) {
    return std::nullopt;
  }
  const auto u = transformed_point.x;
  const auto v = transformed_point.y;
  const auto width = native_extent.width;
  const auto height = native_extent.height;
  switch (transform) {
  case OutputTransform::Normal:
    return PhysicalPoint{u, v};
  case OutputTransform::Rotate90:
    return PhysicalPoint{width - v, u};
  case OutputTransform::Rotate180:
    return PhysicalPoint{width - u, height - v};
  case OutputTransform::Rotate270:
    return PhysicalPoint{v, height - u};
  case OutputTransform::Flipped:
    return PhysicalPoint{width - u, v};
  case OutputTransform::Flipped90:
    return PhysicalPoint{width - v, height - u};
  case OutputTransform::Flipped180:
    return PhysicalPoint{u, height - v};
  case OutputTransform::Flipped270:
    return PhysicalPoint{v, u};
  }
  return std::nullopt;
}

std::optional<PhysicalPoint>
inverse_transform_boundary(const PhysicalPoint native_point,
                           const PhysicalExtent native_extent,
                           const OutputTransform transform) noexcept {
  if (!valid_output_transform(transform) ||
      !contains_boundary(native_extent, native_point)) {
    return std::nullopt;
  }
  const auto x = native_point.x;
  const auto y = native_point.y;
  const auto width = native_extent.width;
  const auto height = native_extent.height;
  switch (transform) {
  case OutputTransform::Normal:
    return PhysicalPoint{x, y};
  case OutputTransform::Rotate90:
    return PhysicalPoint{y, width - x};
  case OutputTransform::Rotate180:
    return PhysicalPoint{width - x, height - y};
  case OutputTransform::Rotate270:
    return PhysicalPoint{height - y, x};
  case OutputTransform::Flipped:
    return PhysicalPoint{width - x, y};
  case OutputTransform::Flipped90:
    return PhysicalPoint{height - y, width - x};
  case OutputTransform::Flipped180:
    return PhysicalPoint{x, height - y};
  case OutputTransform::Flipped270:
    return PhysicalPoint{y, x};
  }
  return std::nullopt;
}

std::optional<PhysicalRectangle>
transform_rectangle(const PhysicalRectangle transformed_rectangle,
                    const PhysicalExtent native_extent,
                    const OutputTransform transform) noexcept {
  const auto transformed_extent =
      transformed_physical_extent(native_extent, transform);
  if (!valid_output_transform(transform) ||
      !contains_rectangle(transformed_extent, transformed_rectangle)) {
    return std::nullopt;
  }
  return map_rectangle_corners(
      transformed_rectangle, [&](const PhysicalPoint point) {
        return transform_boundary(point, native_extent, transform);
      });
}

std::optional<PhysicalRectangle>
inverse_transform_rectangle(const PhysicalRectangle native_rectangle,
                            const PhysicalExtent native_extent,
                            const OutputTransform transform) noexcept {
  if (!valid_output_transform(transform) ||
      !contains_rectangle(native_extent, native_rectangle)) {
    return std::nullopt;
  }
  return map_rectangle_corners(
      native_rectangle, [&](const PhysicalPoint point) {
        return inverse_transform_boundary(point, native_extent, transform);
      });
}

} // namespace glasswyrm::output
