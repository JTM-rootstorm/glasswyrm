#include "output/model/scale.hpp"

#include <limits>
#include <numeric>

namespace glasswyrm::output {
namespace {

[[nodiscard]] constexpr int compare_scales(const RationalScale left,
                                           const RationalScale right) noexcept {
  const auto left_product =
      static_cast<std::uint64_t>(left.numerator) * right.denominator;
  const auto right_product =
      static_cast<std::uint64_t>(right.numerator) * left.denominator;
  return left_product < right_product   ? -1
         : left_product > right_product ? 1
                                        : 0;
}

} // namespace

std::optional<RationalScale>
reduce_scale(const std::uint32_t numerator,
             const std::uint32_t denominator) noexcept {
  if (numerator == 0 || denominator == 0) {
    return std::nullopt;
  }
  const auto divisor = std::gcd(numerator, denominator);
  return RationalScale{numerator / divisor, denominator / divisor};
}

bool is_reduced(const RationalScale scale) noexcept {
  return scale.numerator != 0 && scale.denominator != 0 &&
         std::gcd(scale.numerator, scale.denominator) == 1;
}

bool scale_in_range(const RationalScale value, const RationalScale minimum,
                    const RationalScale maximum) noexcept {
  if (value.numerator == 0 || value.denominator == 0 ||
      minimum.numerator == 0 || minimum.denominator == 0 ||
      maximum.numerator == 0 || maximum.denominator == 0) {
    return false;
  }
  return compare_scales(value, minimum) >= 0 &&
         compare_scales(value, maximum) <= 0;
}

bool valid_output_scale(const RationalScale scale,
                        const std::uint32_t denominator_limit) noexcept {
  return denominator_limit != 0 && scale.denominator <= denominator_limit &&
         scale.denominator <= kMaximumScaleDenominator && is_reduced(scale) &&
         scale_in_range(scale, RationalScale{1, 1}, RationalScale{4, 1});
}

std::optional<std::uint32_t>
derive_logical_dimension(const std::uint32_t transformed_physical_extent,
                         const RationalScale scale) noexcept {
  if (!is_reduced(scale)) {
    return std::nullopt;
  }
  const auto product = static_cast<std::uint64_t>(transformed_physical_extent) *
                       scale.denominator;
  const auto quotient = product / scale.numerator;
  const auto rounded = quotient + (product % scale.numerator != 0 ? 1U : 0U);
  if (rounded > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(rounded);
}

std::optional<LogicalExtent>
derive_logical_extent(const PhysicalExtent transformed_physical_extent,
                      const RationalScale scale) noexcept {
  const auto width =
      derive_logical_dimension(transformed_physical_extent.width, scale);
  const auto height =
      derive_logical_dimension(transformed_physical_extent.height, scale);
  if (!width || !height) {
    return std::nullopt;
  }
  return LogicalExtent{*width, *height};
}

} // namespace glasswyrm::output
