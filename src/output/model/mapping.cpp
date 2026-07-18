#include "output/model/mapping.hpp"

#include "output/model/scale.hpp"
#include "output/model/transform.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::output {
namespace {

using Wide = std::int64_t;

[[nodiscard]] bool logical_bounds(const OutputMapping &mapping, Wide &left,
                                  Wide &top, Wide &right,
                                  Wide &bottom) noexcept {
  left = mapping.logical_origin.x;
  top = mapping.logical_origin.y;
  right = left + mapping.logical_extent.width;
  bottom = top + mapping.logical_extent.height;
  return left >= 0 && top >= 0 &&
         right <= std::numeric_limits<std::int32_t>::max() &&
         bottom <= std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] std::optional<std::uint32_t>
scaled_floor(const std::uint64_t logical, const RationalScale scale) noexcept {
  if (scale.numerator == 0 || scale.denominator == 0 ||
      logical > std::numeric_limits<std::uint64_t>::max() / scale.numerator) {
    return std::nullopt;
  }
  const auto value = logical * scale.numerator / scale.denominator;
  if (value > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::optional<std::uint32_t>
scaled_ceil(const std::uint64_t logical, const RationalScale scale) noexcept {
  if (scale.numerator == 0 || scale.denominator == 0 ||
      logical > std::numeric_limits<std::uint64_t>::max() / scale.numerator) {
    return std::nullopt;
  }
  const auto product = logical * scale.numerator;
  const auto value = product / scale.denominator +
                     (product % scale.denominator != 0 ? 1U : 0U);
  if (value > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::optional<PhysicalRectangle>
expand_bilinear(const PhysicalRectangle rectangle,
                const PhysicalExtent bounds) noexcept {
  if (rectangle.empty() || rectangle.x > bounds.width ||
      rectangle.y > bounds.height ||
      static_cast<std::uint64_t>(rectangle.x) + rectangle.width >
          bounds.width ||
      static_cast<std::uint64_t>(rectangle.y) + rectangle.height >
          bounds.height) {
    return std::nullopt;
  }
  const auto left = rectangle.x == 0 ? 0U : rectangle.x - 1U;
  const auto top = rectangle.y == 0 ? 0U : rectangle.y - 1U;
  const auto right = std::min<std::uint64_t>(
      bounds.width,
      static_cast<std::uint64_t>(rectangle.x) + rectangle.width + 1U);
  const auto bottom = std::min<std::uint64_t>(
      bounds.height,
      static_cast<std::uint64_t>(rectangle.y) + rectangle.height + 1U);
  return PhysicalRectangle{left, top, static_cast<std::uint32_t>(right - left),
                           static_cast<std::uint32_t>(bottom - top)};
}

} // namespace

std::optional<OutputMapping>
make_output_mapping(const OutputState &state) noexcept {
  OutputMapping mapping{{state.logical_x, state.logical_y},
                        {state.logical_width, state.logical_height},
                        {state.physical_width, state.physical_height},
                        state.scale,
                        state.transform};
  if (!state.enabled || !valid_output_mapping(mapping))
    return std::nullopt;
  return mapping;
}

bool valid_output_mapping(const OutputMapping &mapping) noexcept {
  if (mapping.logical_extent.width == 0 || mapping.logical_extent.height == 0 ||
      mapping.physical_extent.width == 0 ||
      mapping.physical_extent.height == 0 ||
      !valid_output_scale(mapping.scale) ||
      !valid_output_transform(mapping.transform)) {
    return false;
  }
  Wide left = 0;
  Wide top = 0;
  Wide right = 0;
  Wide bottom = 0;
  if (!logical_bounds(mapping, left, top, right, bottom))
    return false;
  const auto transformed =
      transformed_physical_extent(mapping.physical_extent, mapping.transform);
  const auto derived = derive_logical_extent(transformed, mapping.scale);
  return derived && *derived == mapping.logical_extent;
}

std::optional<PhysicalPoint>
map_logical_point_to_native(const OutputMapping &mapping,
                            const LogicalPoint point) noexcept {
  if (!valid_output_mapping(mapping))
    return std::nullopt;
  Wide left = 0;
  Wide top = 0;
  Wide right = 0;
  Wide bottom = 0;
  if (!logical_bounds(mapping, left, top, right, bottom) || point.x < left ||
      point.x > right || point.y < top || point.y > bottom) {
    return std::nullopt;
  }
  const auto transformed_extent =
      transformed_physical_extent(mapping.physical_extent, mapping.transform);
  const auto x =
      scaled_floor(static_cast<std::uint64_t>(point.x - left), mapping.scale);
  const auto y =
      scaled_floor(static_cast<std::uint64_t>(point.y - top), mapping.scale);
  if (!x || !y)
    return std::nullopt;
  const PhysicalPoint clipped{std::min(*x, transformed_extent.width),
                              std::min(*y, transformed_extent.height)};
  return transform_boundary(clipped, mapping.physical_extent,
                            mapping.transform);
}

std::optional<PhysicalRectangle>
map_logical_rectangle_to_native(const OutputMapping &mapping,
                                const LogicalRectangle rectangle) noexcept {
  if (!valid_output_mapping(mapping) || rectangle.empty())
    return std::nullopt;
  Wide output_left = 0;
  Wide output_top = 0;
  Wide output_right = 0;
  Wide output_bottom = 0;
  if (!logical_bounds(mapping, output_left, output_top, output_right,
                      output_bottom)) {
    return std::nullopt;
  }
  const Wide rectangle_right = static_cast<Wide>(rectangle.x) + rectangle.width;
  const Wide rectangle_bottom =
      static_cast<Wide>(rectangle.y) + rectangle.height;
  const Wide left = std::max<Wide>(rectangle.x, output_left);
  const Wide top = std::max<Wide>(rectangle.y, output_top);
  const Wide right = std::min(rectangle_right, output_right);
  const Wide bottom = std::min(rectangle_bottom, output_bottom);
  if (left >= right || top >= bottom)
    return std::nullopt;

  const auto transformed_extent =
      transformed_physical_extent(mapping.physical_extent, mapping.transform);
  const auto x0 = scaled_floor(static_cast<std::uint64_t>(left - output_left),
                               mapping.scale);
  const auto y0 =
      scaled_floor(static_cast<std::uint64_t>(top - output_top), mapping.scale);
  const auto x1 = scaled_ceil(static_cast<std::uint64_t>(right - output_left),
                              mapping.scale);
  const auto y1 = scaled_ceil(static_cast<std::uint64_t>(bottom - output_top),
                              mapping.scale);
  if (!x0 || !y0 || !x1 || !y1)
    return std::nullopt;
  const auto clipped_x0 = std::min(*x0, transformed_extent.width);
  const auto clipped_y0 = std::min(*y0, transformed_extent.height);
  const auto clipped_x1 = std::min(*x1, transformed_extent.width);
  const auto clipped_y1 = std::min(*y1, transformed_extent.height);
  if (clipped_x0 >= clipped_x1 || clipped_y0 >= clipped_y1)
    return std::nullopt;
  return transform_rectangle({clipped_x0, clipped_y0, clipped_x1 - clipped_x0,
                              clipped_y1 - clipped_y0},
                             mapping.physical_extent, mapping.transform);
}

std::optional<LogicalSamplePoint>
map_native_pixel_center_to_logical(const OutputMapping &mapping,
                                   const PhysicalPoint pixel) noexcept {
  if (!valid_output_mapping(mapping) ||
      pixel.x >= mapping.physical_extent.width ||
      pixel.y >= mapping.physical_extent.height) {
    return std::nullopt;
  }
  const auto transformed_pixel = inverse_transform_rectangle(
      {pixel.x, pixel.y, 1, 1}, mapping.physical_extent, mapping.transform);
  if (!transformed_pixel ||
      mapping.scale.numerator >
          std::numeric_limits<std::uint32_t>::max() / 2U) {
    return std::nullopt;
  }
  const auto denominator = mapping.scale.numerator * 2U;
  const auto transformed_x_twice =
      static_cast<std::uint64_t>(transformed_pixel->x) * 2U + 1U;
  const auto transformed_y_twice =
      static_cast<std::uint64_t>(transformed_pixel->y) * 2U + 1U;
  if (transformed_x_twice >
          static_cast<std::uint64_t>(std::numeric_limits<Wide>::max()) /
              mapping.scale.denominator ||
      transformed_y_twice >
          static_cast<std::uint64_t>(std::numeric_limits<Wide>::max()) /
              mapping.scale.denominator) {
    return std::nullopt;
  }
  const Wide local_x =
      static_cast<Wide>(transformed_x_twice * mapping.scale.denominator);
  const Wide local_y =
      static_cast<Wide>(transformed_y_twice * mapping.scale.denominator);
  const Wide origin_x =
      static_cast<Wide>(mapping.logical_origin.x) * denominator;
  const Wide origin_y =
      static_cast<Wide>(mapping.logical_origin.y) * denominator;
  return LogicalSamplePoint{origin_x + local_x, origin_y + local_y,
                            denominator};
}

std::optional<PhysicalRectangle>
map_logical_damage_to_native(const OutputMapping &mapping,
                             const LogicalRectangle damage,
                             const DamageFilterFootprint footprint) noexcept {
  if (footprint != DamageFilterFootprint::Point &&
      footprint != DamageFilterFootprint::Bilinear) {
    return std::nullopt;
  }
  const auto mapped = map_logical_rectangle_to_native(mapping, damage);
  if (!mapped || footprint == DamageFilterFootprint::Point)
    return mapped;
  return expand_bilinear(*mapped, mapping.physical_extent);
}

} // namespace glasswyrm::output
