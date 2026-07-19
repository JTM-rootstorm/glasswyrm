#include "output/model/mapping.hpp"
#include "output/model/transform.hpp"

#include "helpers/test_support.hpp"

#include <array>

namespace {

using namespace glasswyrm::output;
using gw::test::require;

OutputMapping mapping(const OutputTransform transform,
                      const PhysicalExtent physical = {4, 3},
                      const RationalScale scale = {1, 1}) {
  const auto transformed = transform_swaps_axes(transform)
                               ? PhysicalExtent{physical.height, physical.width}
                               : physical;
  const auto width = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(transformed.width) * scale.denominator +
       scale.numerator - 1U) /
      scale.numerator);
  const auto height = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(transformed.height) * scale.denominator +
       scale.numerator - 1U) /
      scale.numerator);
  return {{10, 20}, {width, height}, physical, scale, transform};
}

void test_point_footprint_has_no_padding() {
  const auto direct = map_logical_damage_to_native(
      mapping(OutputTransform::Normal), LogicalRectangle{11, 21, 2, 1});
  require(direct && *direct == PhysicalRectangle{1, 1, 2, 1},
          "direct and nearest damage use the exact mapped rectangle");
}

void test_bilinear_padding_and_edge_clipping() {
  const auto interior = map_logical_damage_to_native(
      mapping(OutputTransform::Normal, {6, 6}), LogicalRectangle{12, 22, 2, 2},
      DamageFilterFootprint::Bilinear);
  require(interior && *interior == PhysicalRectangle{1, 1, 4, 4},
          "bilinear damage adds one native pixel on every side");

  const auto top_left = map_logical_damage_to_native(
      mapping(OutputTransform::Normal), LogicalRectangle{10, 20, 1, 1},
      DamageFilterFootprint::Bilinear);
  require(top_left && *top_left == PhysicalRectangle{0, 0, 2, 2},
          "bilinear expansion clips at top and left edges");
  const auto bottom_right = map_logical_damage_to_native(
      mapping(OutputTransform::Normal), LogicalRectangle{13, 22, 1, 1},
      DamageFilterFootprint::Bilinear);
  require(bottom_right && *bottom_right == PhysicalRectangle{2, 1, 2, 2},
          "bilinear expansion clips at bottom and right edges");
}

void test_fractional_damage_expands_in_native_space() {
  const auto fractional = map_logical_damage_to_native(
      mapping(OutputTransform::Normal, {5, 5}, {3, 2}),
      LogicalRectangle{11, 21, 1, 1}, DamageFilterFootprint::Bilinear);
  require(fractional && *fractional == PhysicalRectangle{0, 0, 4, 4},
          "fractional floor-ceil mapping precedes native bilinear padding");

  const auto edge = map_logical_damage_to_native(
      mapping(OutputTransform::Normal, {5, 5}, {3, 2}),
      LogicalRectangle{13, 23, 1, 1}, DamageFilterFootprint::Bilinear);
  require(edge && *edge == PhysicalRectangle{3, 3, 2, 2},
          "fractional remainder and padding both clip to native bounds");
}

void test_transformed_damage_padding() {
  struct Case {
    OutputTransform transform;
    PhysicalRectangle expected;
  };
  constexpr std::array cases{
      Case{OutputTransform::Normal, {0, 0, 4, 3}},
      Case{OutputTransform::Rotate90, {1, 0, 3, 3}},
      Case{OutputTransform::Rotate180, {0, 0, 4, 3}},
      Case{OutputTransform::Rotate270, {0, 0, 3, 3}},
      Case{OutputTransform::Flipped, {0, 0, 4, 3}},
      Case{OutputTransform::Flipped90, {1, 0, 3, 3}},
      Case{OutputTransform::Flipped180, {0, 0, 4, 3}},
      Case{OutputTransform::Flipped270, {0, 0, 3, 3}},
  };
  for (const auto &test : cases) {
    const auto transformed = mapping(test.transform);
    const auto damage = map_logical_damage_to_native(
        transformed, LogicalRectangle{11, 21, 2, 1},
        DamageFilterFootprint::Bilinear);
    require(damage && *damage == test.expected,
            "damage transforms before native-space bilinear padding");
  }
}

void test_invalid_or_empty_damage() {
  require(!map_logical_damage_to_native(mapping(OutputTransform::Normal),
                                        LogicalRectangle{11, 21, 0, 1},
                                        DamageFilterFootprint::Bilinear),
          "empty damage maps to no native rectangle");
  require(!map_logical_damage_to_native(mapping(OutputTransform::Normal),
                                        LogicalRectangle{11, 21, 1, 1},
                                        static_cast<DamageFilterFootprint>(2)),
          "unknown filter footprints are rejected");
}

} // namespace

int main() {
  test_point_footprint_has_no_padding();
  test_bilinear_padding_and_edge_clipping();
  test_fractional_damage_expands_in_native_space();
  test_transformed_damage_padding();
  test_invalid_or_empty_damage();
  return 0;
}
