#include "helpers/test_support.hpp"
#include "output/model/scale.hpp"

#include <cstdint>
#include <limits>

namespace {

using glasswyrm::output::LogicalExtent;
using glasswyrm::output::PhysicalExtent;
using glasswyrm::output::RationalScale;

#define GW_EXPECT(...) ::gw::test::require((__VA_ARGS__), #__VA_ARGS__)
#define GW_EXPECT_EQ(actual, ...)                                              \
  ::gw::test::require((actual) == (__VA_ARGS__), #actual " == " #__VA_ARGS__)

void test_reduction_and_validation() {
  const auto reduced = glasswyrm::output::reduce_scale(10, 8);
  GW_EXPECT(reduced.has_value());
  GW_EXPECT_EQ(*reduced, RationalScale{5, 4});
  GW_EXPECT(!glasswyrm::output::reduce_scale(0, 1));
  GW_EXPECT(!glasswyrm::output::reduce_scale(1, 0));

  GW_EXPECT(glasswyrm::output::is_reduced(RationalScale{1, 1}));
  GW_EXPECT(glasswyrm::output::is_reduced(RationalScale{5, 4}));
  GW_EXPECT(!glasswyrm::output::is_reduced(RationalScale{2, 2}));
  GW_EXPECT(!glasswyrm::output::is_reduced(RationalScale{0, 1}));
  GW_EXPECT(!glasswyrm::output::is_reduced(RationalScale{1, 0}));

  GW_EXPECT(glasswyrm::output::valid_output_scale(RationalScale{1, 1}));
  GW_EXPECT(glasswyrm::output::valid_output_scale(RationalScale{5, 4}));
  GW_EXPECT(glasswyrm::output::valid_output_scale(RationalScale{4, 3}));
  GW_EXPECT(glasswyrm::output::valid_output_scale(RationalScale{4, 1}));
  GW_EXPECT(!glasswyrm::output::valid_output_scale(RationalScale{3, 4}));
  GW_EXPECT(!glasswyrm::output::valid_output_scale(RationalScale{2, 2}));
  GW_EXPECT(
      !glasswyrm::output::valid_output_scale(RationalScale{121, 120}, 119));
  GW_EXPECT(
      !glasswyrm::output::valid_output_scale(RationalScale{122, 121}, 1000));
  GW_EXPECT(!glasswyrm::output::valid_output_scale(RationalScale{481, 120}));
}

void test_range_comparison() {
  GW_EXPECT(glasswyrm::output::scale_in_range(
      RationalScale{5, 4}, RationalScale{1, 1}, RationalScale{4, 3}));
  GW_EXPECT(!glasswyrm::output::scale_in_range(
      RationalScale{6, 5}, RationalScale{5, 4}, RationalScale{2, 1}));
  GW_EXPECT(!glasswyrm::output::scale_in_range(
      RationalScale{2, 1}, RationalScale{1, 1}, RationalScale{3, 2}));
  GW_EXPECT(!glasswyrm::output::scale_in_range(
      RationalScale{0, 1}, RationalScale{1, 1}, RationalScale{4, 1}));
}

void test_logical_derivation() {
  GW_EXPECT_EQ(glasswyrm::output::derive_logical_extent(
                   PhysicalExtent{800, 600}, RationalScale{5, 4}),
               LogicalExtent{640, 480});
  GW_EXPECT_EQ(glasswyrm::output::derive_logical_extent(
                   PhysicalExtent{1024, 768}, RationalScale{4, 3}),
               LogicalExtent{768, 576});
  GW_EXPECT_EQ(glasswyrm::output::derive_logical_extent(
                   PhysicalExtent{600, 800}, RationalScale{5, 4}),
               LogicalExtent{480, 640});

  const auto remainder =
      glasswyrm::output::derive_logical_dimension(3, RationalScale{2, 1});
  GW_EXPECT(remainder.has_value());
  GW_EXPECT_EQ(*remainder, 2U);
  GW_EXPECT_EQ(
      *glasswyrm::output::derive_logical_dimension(7, RationalScale{3, 2}), 5U);

  GW_EXPECT(
      !glasswyrm::output::derive_logical_dimension(10, RationalScale{2, 2}));
  GW_EXPECT(!glasswyrm::output::derive_logical_dimension(
      std::numeric_limits<std::uint32_t>::max(),
      RationalScale{1, std::numeric_limits<std::uint32_t>::max()}));
}

} // namespace

int main() {
  test_reduction_and_validation();
  test_range_comparison();
  test_logical_derivation();
  return 0;
}
