#include "helpers/test_support.hpp"
#include "output/model/transform.hpp"

#include <array>
#include <cstddef>

namespace {

using glasswyrm::output::OutputTransform;
using glasswyrm::output::PhysicalExtent;
using glasswyrm::output::PhysicalPoint;
using glasswyrm::output::PhysicalRectangle;

#define GW_EXPECT(...) ::gw::test::require((__VA_ARGS__), #__VA_ARGS__)
#define GW_EXPECT_EQ(actual, ...)                                              \
  ::gw::test::require((actual) == (__VA_ARGS__), #actual " == " #__VA_ARGS__)

constexpr std::array kTransforms{
    OutputTransform::Normal,     OutputTransform::Rotate90,
    OutputTransform::Rotate180,  OutputTransform::Rotate270,
    OutputTransform::Flipped,    OutputTransform::Flipped90,
    OutputTransform::Flipped180, OutputTransform::Flipped270,
};

void test_transformed_extents() {
  constexpr PhysicalExtent native{5, 3};
  for (const auto transform : kTransforms) {
    const bool swaps = transform == OutputTransform::Rotate90 ||
                       transform == OutputTransform::Rotate270 ||
                       transform == OutputTransform::Flipped90 ||
                       transform == OutputTransform::Flipped270;
    GW_EXPECT_EQ(glasswyrm::output::transform_swaps_axes(transform), swaps);
    GW_EXPECT_EQ(
        glasswyrm::output::transformed_physical_extent(native, transform),
        swaps ? PhysicalExtent{3, 5} : native);
  }
}

void test_boundary_formulas_and_inverse() {
  constexpr PhysicalExtent native{5, 3};
  constexpr std::array expected{
      PhysicalPoint{1, 2}, PhysicalPoint{3, 1}, PhysicalPoint{4, 1},
      PhysicalPoint{2, 2}, PhysicalPoint{4, 2}, PhysicalPoint{3, 2},
      PhysicalPoint{1, 1}, PhysicalPoint{2, 1},
  };
  for (std::size_t index = 0; index < kTransforms.size(); ++index) {
    const auto mapped = glasswyrm::output::transform_boundary(
        PhysicalPoint{1, 2}, native, kTransforms[index]);
    GW_EXPECT(mapped.has_value());
    GW_EXPECT_EQ(*mapped, expected[index]);
    GW_EXPECT_EQ(glasswyrm::output::inverse_transform_boundary(
                     *mapped, native, kTransforms[index]),
                 PhysicalPoint{1, 2});

    const auto extent = glasswyrm::output::transformed_physical_extent(
        native, kTransforms[index]);
    for (std::uint32_t y = 0; y <= extent.height; ++y) {
      for (std::uint32_t x = 0; x <= extent.width; ++x) {
        const PhysicalPoint point{x, y};
        const auto forward = glasswyrm::output::transform_boundary(
            point, native, kTransforms[index]);
        GW_EXPECT(forward.has_value());
        GW_EXPECT_EQ(glasswyrm::output::inverse_transform_boundary(
                         *forward, native, kTransforms[index]),
                     point);
      }
    }
  }
}

void test_half_open_rectangles() {
  constexpr PhysicalExtent native{5, 3};
  constexpr PhysicalRectangle source{1, 0, 2, 1};
  constexpr std::array expected{
      PhysicalRectangle{1, 0, 2, 1}, PhysicalRectangle{4, 1, 1, 2},
      PhysicalRectangle{2, 2, 2, 1}, PhysicalRectangle{0, 0, 1, 2},
      PhysicalRectangle{2, 0, 2, 1}, PhysicalRectangle{4, 0, 1, 2},
      PhysicalRectangle{1, 2, 2, 1}, PhysicalRectangle{0, 1, 1, 2},
  };
  for (std::size_t index = 0; index < kTransforms.size(); ++index) {
    const auto mapped = glasswyrm::output::transform_rectangle(
        source, native, kTransforms[index]);
    GW_EXPECT(mapped.has_value());
    GW_EXPECT_EQ(*mapped, expected[index]);
    GW_EXPECT_EQ(glasswyrm::output::inverse_transform_rectangle(
                     *mapped, native, kTransforms[index]),
                 source);

    const auto transformed_extent =
        glasswyrm::output::transformed_physical_extent(native,
                                                       kTransforms[index]);
    const auto full = glasswyrm::output::transform_rectangle(
        PhysicalRectangle{0, 0, transformed_extent.width,
                          transformed_extent.height},
        native, kTransforms[index]);
    GW_EXPECT_EQ(full, PhysicalRectangle{0, 0, native.width, native.height});
  }
}

void test_invalid_coordinates_and_transform() {
  constexpr PhysicalExtent native{5, 3};
  GW_EXPECT(!glasswyrm::output::transform_boundary(PhysicalPoint{6, 0}, native,
                                                   OutputTransform::Normal));
  GW_EXPECT(!glasswyrm::output::transform_rectangle(
      PhysicalRectangle{4, 0, 2, 1}, native, OutputTransform::Normal));
  GW_EXPECT(!glasswyrm::output::inverse_transform_rectangle(
      PhysicalRectangle{0, 2, 1, 2}, native, OutputTransform::Normal));

  const auto invalid = static_cast<OutputTransform>(255);
  GW_EXPECT(!glasswyrm::output::valid_output_transform(invalid));
  GW_EXPECT_EQ(glasswyrm::output::output_transform_bit(invalid), 0U);
  GW_EXPECT_EQ(glasswyrm::output::transformed_physical_extent(native, invalid),
               PhysicalExtent{});
  GW_EXPECT(
      !glasswyrm::output::transform_boundary(PhysicalPoint{}, native, invalid));
}

} // namespace

int main() {
  test_transformed_extents();
  test_boundary_formulas_and_inverse();
  test_half_open_rectangles();
  test_invalid_coordinates_and_transform();
  return 0;
}
