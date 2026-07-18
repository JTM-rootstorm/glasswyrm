#pragma once

#include "output/model/types.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::output {

// Geometry needed to map global logical desktop coordinates into one output's
// native scanout pixels. The logical extent must be the exact ceil-derived
// extent for physical_extent, scale, and transform.
struct OutputMapping {
  LogicalPoint logical_origin{};
  LogicalExtent logical_extent{};
  PhysicalExtent physical_extent{};
  RationalScale scale{};
  OutputTransform transform{OutputTransform::Normal};

  friend constexpr bool operator==(const OutputMapping &,
                                   const OutputMapping &) = default;
};

// An exact logical sampling position. Both numerators use the common positive
// denominator; this avoids floating-point drift at fractional output scales.
struct LogicalSamplePoint {
  std::int64_t x_numerator{};
  std::int64_t y_numerator{};
  std::uint32_t denominator{1};

  friend constexpr bool operator==(const LogicalSamplePoint &,
                                   const LogicalSamplePoint &) = default;
};

enum class DamageFilterFootprint : std::uint8_t {
  Point = 0,
  Bilinear = 1,
};

[[nodiscard]] std::optional<OutputMapping>
make_output_mapping(const OutputState &state) noexcept;

[[nodiscard]] bool valid_output_mapping(const OutputMapping &mapping) noexcept;

// Maps an integer logical boundary using floor scale rounding. The right and
// bottom logical edges are accepted and can map to native half-open edges.
[[nodiscard]] std::optional<PhysicalPoint>
map_logical_point_to_native(const OutputMapping &mapping,
                            LogicalPoint point) noexcept;

// Maps a global logical half-open rectangle. The input is first clipped to the
// output's logical rectangle, then lower bounds are floored and upper bounds
// are ceiled before the output transform is applied.
[[nodiscard]] std::optional<PhysicalRectangle>
map_logical_rectangle_to_native(const OutputMapping &mapping,
                                LogicalRectangle rectangle) noexcept;

// Returns the exact global logical coordinate at a native pixel center.
[[nodiscard]] std::optional<LogicalSamplePoint>
map_native_pixel_center_to_logical(const OutputMapping &mapping,
                                   PhysicalPoint pixel) noexcept;

// Damage uses the same conservative half-open rectangle mapping. Bilinear
// filtering adds one native pixel on every side after transformation and then
// clips the result to the native output bounds.
[[nodiscard]] std::optional<PhysicalRectangle> map_logical_damage_to_native(
    const OutputMapping &mapping, LogicalRectangle damage,
    DamageFilterFootprint footprint = DamageFilterFootprint::Point) noexcept;

} // namespace glasswyrm::output
