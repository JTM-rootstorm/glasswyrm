#pragma once

#include "output/model/types.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::output {

[[nodiscard]] std::optional<RationalScale>
reduce_scale(std::uint32_t numerator, std::uint32_t denominator) noexcept;

[[nodiscard]] bool is_reduced(RationalScale scale) noexcept;

[[nodiscard]] bool scale_in_range(RationalScale value, RationalScale minimum,
                                  RationalScale maximum) noexcept;

[[nodiscard]] bool valid_output_scale(
    RationalScale scale,
    std::uint32_t denominator_limit = kMaximumScaleDenominator) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
derive_logical_dimension(std::uint32_t transformed_physical_extent,
                         RationalScale scale) noexcept;

[[nodiscard]] std::optional<LogicalExtent>
derive_logical_extent(PhysicalExtent transformed_physical_extent,
                      RationalScale scale) noexcept;

} // namespace glasswyrm::output
