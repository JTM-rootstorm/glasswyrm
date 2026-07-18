#include "output/model/mapping.hpp"
#include "output/model/transform.hpp"

#include "helpers/test_support.hpp"

#include <array>
#include <cstdint>

namespace {

using namespace glasswyrm::output;
using gw::test::require;

OutputMapping mapping(const OutputTransform transform,
                      const PhysicalExtent physical = {4, 3},
                      const RationalScale scale = {1, 1},
                      const LogicalPoint origin = {10, 20}) {
  const auto transformed = transform_swaps_axes(transform)
                               ? PhysicalExtent{physical.height, physical.width}
                               : physical;
  const auto logical_width = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(transformed.width) * scale.denominator +
       scale.numerator - 1U) /
      scale.numerator);
  const auto logical_height = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(transformed.height) * scale.denominator +
       scale.numerator - 1U) /
      scale.numerator);
  return {origin, {logical_width, logical_height}, physical, scale, transform};
}

void test_mapping_validation_and_state_adapter() {
  auto state = OutputState{};
  state.enabled = true;
  state.logical_x = 10;
  state.logical_y = 20;
  state.logical_width = 2;
  state.logical_height = 2;
  state.physical_width = 4;
  state.physical_height = 4;
  state.scale = {2, 1};
  const auto from_state = make_output_mapping(state);
  require(from_state && valid_output_mapping(*from_state),
          "enabled exact-size output state creates a mapping");
  state.logical_width = 3;
  require(!make_output_mapping(state),
          "mapping rejects a non-derived logical extent");
  state.logical_width = 2;
  state.enabled = false;
  require(!make_output_mapping(state), "disabled output has no mapping");

  auto invalid = mapping(OutputTransform::Normal);
  invalid.logical_origin.x = -1;
  require(!valid_output_mapping(invalid),
          "mapping rejects a negative output position");
  invalid = mapping(OutputTransform::Normal);
  invalid.scale = {2, 2};
  require(!valid_output_mapping(invalid),
          "mapping requires a reduced output scale");
}

void test_points_through_all_transforms() {
  struct Case {
    OutputTransform transform;
    PhysicalPoint expected;
  };
  constexpr std::array cases{
      Case{OutputTransform::Normal, {1, 1}},
      Case{OutputTransform::Rotate90, {3, 1}},
      Case{OutputTransform::Rotate180, {3, 2}},
      Case{OutputTransform::Rotate270, {1, 2}},
      Case{OutputTransform::Flipped, {3, 1}},
      Case{OutputTransform::Flipped90, {3, 2}},
      Case{OutputTransform::Flipped180, {1, 2}},
      Case{OutputTransform::Flipped270, {1, 1}},
  };
  for (const auto &test : cases) {
    const auto point = map_logical_point_to_native(mapping(test.transform),
                                                   LogicalPoint{11, 21});
    require(point && *point == test.expected,
            "logical point follows the selected output transform");
  }

  const auto flipped_edge = map_logical_point_to_native(
      mapping(OutputTransform::Flipped), LogicalPoint{14, 23});
  require(flipped_edge && *flipped_edge == PhysicalPoint{0, 3},
          "half-open logical edge maps to a native boundary");
  require(!map_logical_point_to_native(mapping(OutputTransform::Normal),
                                       LogicalPoint{15, 20}),
          "point outside the logical output is rejected");
}

void test_rectangles_through_all_transforms() {
  struct Case {
    OutputTransform transform;
    PhysicalRectangle expected;
  };
  constexpr std::array cases{
      Case{OutputTransform::Normal, {1, 1, 2, 1}},
      Case{OutputTransform::Rotate90, {2, 1, 1, 2}},
      Case{OutputTransform::Rotate180, {1, 1, 2, 1}},
      Case{OutputTransform::Rotate270, {1, 0, 1, 2}},
      Case{OutputTransform::Flipped, {1, 1, 2, 1}},
      Case{OutputTransform::Flipped90, {2, 0, 1, 2}},
      Case{OutputTransform::Flipped180, {1, 1, 2, 1}},
      Case{OutputTransform::Flipped270, {1, 1, 1, 2}},
  };
  for (const auto &test : cases) {
    const auto rectangle = map_logical_rectangle_to_native(
        mapping(test.transform), LogicalRectangle{11, 21, 2, 1});
    require(rectangle && *rectangle == test.expected,
            "logical rectangle follows the selected output transform");
  }
}

void test_floor_ceil_and_clipping() {
  const auto fractional = mapping(OutputTransform::Normal, {5, 5}, {3, 2});
  const auto interior = map_logical_rectangle_to_native(
      fractional, LogicalRectangle{11, 21, 1, 1});
  require(interior && *interior == PhysicalRectangle{1, 1, 2, 2},
          "fractional rectangle floors lower and ceils upper bounds");
  const auto remainder_edge = map_logical_rectangle_to_native(
      fractional, LogicalRectangle{13, 23, 1, 1});
  require(remainder_edge && *remainder_edge == PhysicalRectangle{4, 4, 1, 1},
          "ceil-derived remainder clips to the physical edge");

  const auto clipped = map_logical_rectangle_to_native(
      mapping(OutputTransform::Normal), LogicalRectangle{8, 19, 4, 3});
  require(clipped && *clipped == PhysicalRectangle{0, 0, 2, 2},
          "partially intersecting logical rectangle clips before mapping");
  require(!map_logical_rectangle_to_native(mapping(OutputTransform::Normal),
                                           LogicalRectangle{0, 0, 1, 1}),
          "disjoint logical rectangle maps to no native pixels");
}

void test_inverse_pixel_centers() {
  const auto doubled = mapping(OutputTransform::Normal, {4, 4}, {2, 1});
  const auto first =
      map_native_pixel_center_to_logical(doubled, PhysicalPoint{0, 0});
  const auto last =
      map_native_pixel_center_to_logical(doubled, PhysicalPoint{3, 3});
  require(first && *first == LogicalSamplePoint{41, 81, 4},
          "first native center maps exactly through integer output scale");
  require(last && *last == LogicalSamplePoint{47, 87, 4},
          "last native center maps exactly through integer output scale");

  const auto rotated = map_native_pixel_center_to_logical(
      mapping(OutputTransform::Rotate90), PhysicalPoint{0, 0});
  require(rotated && *rotated == LogicalSamplePoint{21, 47, 2},
          "inverse sampling applies output rotation before scale");
  require(!map_native_pixel_center_to_logical(mapping(OutputTransform::Normal),
                                              PhysicalPoint{4, 0}),
          "pixel centers outside native bounds are rejected");
}

} // namespace

int main() {
  test_mapping_validation_and_state_adapter();
  test_points_through_all_transforms();
  test_rectangles_through_all_transforms();
  test_floor_ceil_and_clipping();
  test_inverse_pixel_centers();
  return 0;
}
