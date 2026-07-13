#include "core/geometry/region.hpp"
#include "glasswyrmd/bitmap_storage.hpp"
#include "glasswyrmd/m9_raster_ops.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/raster_ops.hpp"
#include "helpers/test_support.hpp"

#include <array>

int main() {
  using glasswyrm::geometry::Rectangle;
  using glasswyrm::server::PixelStorage;
  auto bitmap = glasswyrm::server::BitmapStorage::create(9, 3);
  gw::test::require(bitmap.has_value(), "create depth-one storage");
  gw::test::require(bitmap->byte_size() == 27U, "unpacked bitmap size");
  gw::test::require(bitmap->at(8, 2) == 0U, "bitmap initializes clear");
  bitmap->set(8, 2, 3U);
  gw::test::require(bitmap->at(8, 2) == 1U, "bitmap canonicalizes bits");
  auto pixels = PixelStorage::create(4, 3);
  gw::test::require(pixels.has_value(), "create storage");
  gw::test::require(pixels->stride() == 16U, "stride");
  gw::test::require(pixels->at(3, 2) == 0xff000000U, "initial black");
  pixels->fill({-1, 1, 3, 3}, 0x00112233U);
  gw::test::require(pixels->at(0, 1) == 0xff112233U, "clipped fill");
  gw::test::require(pixels->at(2, 1) == 0xff000000U, "outside fill");
  auto resized = pixels->resize_preserving_overlap(5, 4, 0x00445566U);
  gw::test::require(resized.has_value(), "resize");
  gw::test::require(resized->at(0, 1) == 0xff112233U, "preserve overlap");
  gw::test::require(resized->at(4, 3) == 0xff445566U, "initialize growth");

  std::array<std::uint8_t, 8> image{0x03, 0x02, 0x01, 0x00,
                                    0x30, 0x20, 0x10, 0xaa};
  const auto put = glasswyrm::server::put_zpixmap(*pixels, -1, 0, 2, 1, image);
  gw::test::require(put.success, "put image");
  gw::test::require(pixels->at(0, 0) == 0xff102030U, "LSB image decode");

  pixels->fill({0, 0, 4, 1}, 0x00abcdefU);
  const auto copied = glasswyrm::server::copy_area(*pixels, *pixels, 0, 0, 3, 1,
                                                   1, 0);
  gw::test::require(copied.success, "copy area");
  gw::test::require(pixels->at(3, 0) == 0xffabcdefU, "overlap copy");

  auto primitives = PixelStorage::create(12, 12);
  gw::test::require(primitives.has_value(), "create primitive target");
  glasswyrm::server::draw_line(*primitives, {1, 1}, {5, 3}, 0x00ffffffU);
  gw::test::require(primitives->at(1, 1) == 0xffffffffU,
                    "line includes first endpoint");
  gw::test::require(primitives->at(5, 3) == 0xffffffffU,
                    "line includes last endpoint");
  const std::array<glasswyrm::server::RasterSegment, 2> segments{{
      {{0, 11}, {2, 11}}, {{11, 0}, {11, 2}}}};
  glasswyrm::server::draw_segments(*primitives, segments, 0x0000ff00U);
  gw::test::require(primitives->at(2, 11) == 0xff00ff00U,
                    "independent segment endpoint");
  const std::array<glasswyrm::server::RasterPoint, 3> triangle{{
      {2, 4}, {8, 4}, {5, 9}}};
  glasswyrm::server::fill_convex_polygon(*primitives, triangle,
                                          0x00ff0000U);
  gw::test::require(primitives->at(5, 5) == 0xffff0000U,
                    "convex polygon interior");
  glasswyrm::server::fill_ellipse(*primitives, {2, 2, 8, 6}, 0x000000ffU);
  gw::test::require(primitives->at(5, 4) == 0xff0000ffU,
                    "ellipse interior");
  gw::test::require(primitives->at(2, 2) != 0xff0000ffU,
                    "ellipse corner excluded");

  glasswyrm::geometry::Region region({0, 0, 10, 10});
  region.add({-2, -2, 4, 4});
  gw::test::require(region.rectangles().size() == 1U, "region count");
  gw::test::require(region.rectangles()[0] == (Rectangle{0, 0, 2, 2}), "region clip");
  glasswyrm::geometry::Region merged({0, 0, 20, 20});
  merged.add({4, 4, 4, 4});
  merged.add({8, 4, 4, 4});
  merged.add({6, 6, 4, 4});
  gw::test::require(merged.rectangles().size() == 1U,
                    "adjacent and overlapping damage coalesces");
  gw::test::require(merged.rectangles()[0] == (Rectangle{4, 4, 8, 6}),
                    "coalesced damage bounds");
  region.add({2, 0, 3, 2});
  region.add({1, 1, 3, 3});
  gw::test::require(region.rectangles().size() == 1, "region coalesces overlap");
  gw::test::require(region.rectangles()[0] == (Rectangle{0, 0, 5, 4}),
                    "region merges adjacent bounds deterministically");
  for (std::size_t index=0; index<region.rectangles().size(); ++index)
    for (std::size_t other=index+1; other<region.rectangles().size(); ++other)
      gw::test::require(!glasswyrm::geometry::intersect(region.rectangles()[index],region.rectangles()[other]),
                        "normalized region does not overlap");
  gw::test::require(!PixelStorage::create(16384, 16384).has_value(), "storage limit");
  return 0;
}
