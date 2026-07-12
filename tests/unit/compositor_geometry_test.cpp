#include "compositor/damage_region.hpp"
#include "compositor/rectangle.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <limits>

#define GW_EXPECT(...) ::gw::test::require((__VA_ARGS__), #__VA_ARGS__)
#define GW_EXPECT_EQ(actual, ...)                                                \
  ::gw::test::require((actual) == (__VA_ARGS__), #actual " == " #__VA_ARGS__)

using gw::compositor::DamageRegion;
using gw::compositor::Rectangle;

int main() {
  GW_EXPECT_EQ(gw::compositor::intersection(Rectangle{-2, -2, 5, 5},
                                             Rectangle{0, 0, 4, 4}),
               Rectangle{0, 0, 3, 3});
  GW_EXPECT(!gw::compositor::intersection(Rectangle{0, 0, 1, 1},
                                           Rectangle{1, 0, 1, 1}));
  GW_EXPECT(!gw::compositor::has_valid_extents(
      Rectangle{std::numeric_limits<std::int32_t>::max(), 0, 1, 1}));
  GW_EXPECT(!gw::compositor::translate(
      Rectangle{std::numeric_limits<std::int32_t>::max() - 1, 0, 1, 1}, 1, 0));

  DamageRegion region(Rectangle{0, 0, 100, 100});
  region.add(Rectangle{-10, 5, 20, 10});
  region.add(Rectangle{10, 5, 5, 10});
  GW_EXPECT_EQ(region.rectangles().size(), 1U);
  GW_EXPECT_EQ(region.rectangles().front(), Rectangle{0, 5, 15, 10});

  region.add(Rectangle{50, 40, 2, 2});
  GW_EXPECT_EQ(region.rectangles().size(), 2U);
  GW_EXPECT_EQ(region.rectangles()[0], Rectangle{0, 5, 15, 10});
  GW_EXPECT_EQ(region.rectangles()[1], Rectangle{50, 40, 2, 2});

  DamageRegion complex(Rectangle{0, 0, 10000, 2});
  for (std::size_t index = 0; index <= DamageRegion::maximum_rectangles; ++index) {
    complex.add(Rectangle{static_cast<std::int32_t>(index * 2), 0, 1, 1});
  }
  GW_EXPECT(complex.is_full_output());
  GW_EXPECT_EQ(complex.rectangles().front(), Rectangle{0, 0, 10000, 2});
  return 0;
}
