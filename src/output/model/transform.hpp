#pragma once

#include "output/model/types.hpp"

#include <optional>

namespace glasswyrm::output {

[[nodiscard]] bool valid_output_transform(OutputTransform transform) noexcept;

[[nodiscard]] bool transform_swaps_axes(OutputTransform transform) noexcept;

[[nodiscard]] PhysicalExtent
transformed_physical_extent(PhysicalExtent native_extent,
                            OutputTransform transform) noexcept;

// Boundary points are allowed to lie on the right or bottom half-open edge.
// Flipped variants reflect horizontally in transformed space before rotation.
[[nodiscard]] std::optional<PhysicalPoint>
transform_boundary(PhysicalPoint transformed_point,
                   PhysicalExtent native_extent,
                   OutputTransform transform) noexcept;

[[nodiscard]] std::optional<PhysicalPoint>
inverse_transform_boundary(PhysicalPoint native_point,
                           PhysicalExtent native_extent,
                           OutputTransform transform) noexcept;

[[nodiscard]] std::optional<PhysicalRectangle>
transform_rectangle(PhysicalRectangle transformed_rectangle,
                    PhysicalExtent native_extent,
                    OutputTransform transform) noexcept;

[[nodiscard]] std::optional<PhysicalRectangle>
inverse_transform_rectangle(PhysicalRectangle native_rectangle,
                            PhysicalExtent native_extent,
                            OutputTransform transform) noexcept;

} // namespace glasswyrm::output
