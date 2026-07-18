#include "glasswyrmd/picture.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace {

using glasswyrm::geometry::Rectangle;
using glasswyrm::server::DrawablePictureSource;
using glasswyrm::server::Picture;
using glasswyrm::server::PictureAttributeUpdate;
using glasswyrm::server::PictureFormatId;
using glasswyrm::server::PictureStatus;
using glasswyrm::server::PremultipliedColor;
using glasswyrm::server::SolidPictureSource;
using glasswyrm::server::find_picture_format;
using glasswyrm::server::kCanonicalPictureFormats;
using gw::test::require;

void test_canonical_formats() {
  require(kCanonicalPictureFormats.size() == 4,
          "exactly four canonical picture formats are exposed");
  require(static_cast<std::uint32_t>(PictureFormatId::A1) == 0x1FFFF101U &&
              static_cast<std::uint32_t>(PictureFormatId::A8) == 0x1FFFF102U &&
              static_cast<std::uint32_t>(PictureFormatId::Xrgb32) ==
                  0x1FFFF103U &&
              static_cast<std::uint32_t>(PictureFormatId::Argb32) ==
                  0x1FFFF104U,
          "canonical picture format IDs remain stable");

  const auto* a1 = find_picture_format(PictureFormatId::A1);
  const auto* a8 = find_picture_format(PictureFormatId::A8);
  const auto* xrgb = find_picture_format(PictureFormatId::Xrgb32);
  const auto* argb = find_picture_format(PictureFormatId::Argb32);
  require(a1 && a1->depth == 1 && a1->bits_per_pixel == 1 &&
              a1->alpha_mask == 1,
          "A1 descriptor is canonical");
  require(a8 && a8->depth == 8 && a8->bits_per_pixel == 8 &&
              a8->alpha_mask == 0xFF,
          "A8 descriptor is canonical");
  require(xrgb && xrgb->depth == 24 && xrgb->bits_per_pixel == 32 &&
              xrgb->red_shift == 16 && xrgb->green_shift == 8 &&
              xrgb->blue_shift == 0 && xrgb->alpha_mask == 0,
          "XRGB32 descriptor is canonical");
  require(argb && argb->depth == 32 && argb->bits_per_pixel == 32 &&
              argb->alpha_shift == 24 && argb->alpha_mask == 0xFF,
          "ARGB32 descriptor is canonical");
  require(find_picture_format(static_cast<PictureFormatId>(0x12345678U)) ==
              nullptr,
          "unknown picture formats are not described");
}

void test_picture_sources() {
  auto drawable = Picture::create_drawable(44, PictureFormatId::Xrgb32, 24, 32);
  require(drawable.has_value(), "matching drawable picture is created");
  const auto* drawable_source =
      std::get_if<DrawablePictureSource>(&drawable->source());
  require(drawable->format() == PictureFormatId::Xrgb32 && drawable_source &&
              drawable_source->drawable == 44,
          "drawable picture retains its format and drawable ID");
  require(!Picture::create_drawable(44, PictureFormatId::Xrgb32, 32, 32),
          "drawable depth must match the picture format");
  require(!Picture::create_drawable(44, PictureFormatId::A8, 8, 32),
          "drawable bits per pixel must match the picture format");
  require(!Picture::create_drawable(0, PictureFormatId::A8, 8, 8),
          "None is not a drawable picture source");

  auto solid = Picture::create_solid({64, 32, 16, 128});
  require(solid.has_value() && solid->format() == PictureFormatId::Argb32,
          "solid pictures use canonical premultiplied ARGB32");
  const auto* solid_source = std::get_if<SolidPictureSource>(&solid->source());
  require(solid_source &&
              solid_source->color == PremultipliedColor{64, 32, 16, 128},
          "solid picture retains its premultiplied color");
  require(!Picture::create_solid({129, 0, 0, 128}),
          "non-premultiplied solid color is rejected");
}

void test_atomic_attributes() {
  auto picture = Picture::create_drawable(55, PictureFormatId::Argb32, 32, 32);
  require(picture.has_value(), "attribute test picture is created");

  PictureAttributeUpdate supported;
  supported.repeat = 0;
  supported.alpha_map = 0;
  supported.clip_x_origin = -7;
  supported.clip_y_origin = 9;
  supported.component_alpha = 0;
  supported.subwindow_mode = 0;
  require(picture->apply_attributes(supported) == PictureStatus::Success &&
              picture->attributes().clip_x_origin == -7 &&
              picture->attributes().clip_y_origin == 9,
          "supported fixed attributes and clip origin apply");

  PictureAttributeUpdate invalid;
  invalid.clip_x_origin = 500;
  invalid.repeat = 1;
  require(picture->apply_attributes(invalid) ==
              PictureStatus::BadAttributeValue &&
              picture->attributes().clip_x_origin == -7,
          "bad repeat mode cannot partially change clip origin");
  invalid = {};
  invalid.clip_y_origin = 500;
  invalid.unsupported_mask = 1U << 10U;
  require(picture->apply_attributes(invalid) ==
              PictureStatus::UnsupportedAttribute &&
              picture->attributes().clip_y_origin == 9,
          "unsupported attribute mask cannot partially mutate a picture");
  invalid = {};
  invalid.alpha_map = 1;
  require(picture->apply_attributes(invalid) ==
              PictureStatus::BadAttributeValue,
          "non-None alpha map is rejected");
  invalid = {};
  invalid.component_alpha = 1;
  require(picture->apply_attributes(invalid) ==
              PictureStatus::BadAttributeValue,
          "component alpha is rejected");
}

void test_atomic_clip_rectangles() {
  auto picture = Picture::create_drawable(66, PictureFormatId::A8, 8, 8);
  require(picture.has_value(), "clip test picture is created");
  const std::vector<Rectangle> first{{-2, 3, 4, 5}, {8, 9, 10, 11}};
  require(picture->set_clip_rectangles(12, -13, first) ==
              PictureStatus::Success &&
              picture->attributes().clip_rectangles == first &&
              picture->attributes().clip_x_origin == 12 &&
              picture->attributes().clip_y_origin == -13,
          "bounded clip rectangles and request origin apply atomically");

  const std::vector<Rectangle> overflowing{{
      std::numeric_limits<std::int32_t>::max(), 0, 2, 1}};
  require(picture->set_clip_rectangles(99, 100, overflowing) ==
              PictureStatus::InvalidClipRectangle &&
              picture->attributes().clip_rectangles == first &&
              picture->attributes().clip_x_origin == 12,
          "overflowing clip replacement preserves prior clip state");

  PictureAttributeUpdate overflowing_origin;
  overflowing_origin.clip_x_origin =
      std::numeric_limits<std::int32_t>::max();
  require(picture->apply_attributes(overflowing_origin) ==
              PictureStatus::BadAttributeValue &&
              picture->attributes().clip_x_origin == 12,
          "invalid clip-origin translation preserves prior attributes");

  const std::vector<Rectangle> too_many(Picture::kMaximumClipRectangles + 1,
                                        {0, 0, 1, 1});
  require(picture->set_clip_rectangles(99, 100, too_many) ==
              PictureStatus::TooManyClipRectangles &&
              picture->attributes().clip_rectangles == first &&
              picture->attributes().clip_y_origin == -13,
          "oversized clip replacement preserves prior clip state");

  picture->clear_clip();
  require(!picture->attributes().clip_rectangles &&
              picture->attributes().clip_x_origin == 12,
          "clearing the clip preserves independent clip origin attributes");
  const std::vector<Rectangle> empty;
  require(picture->set_clip_rectangles(1, 2, empty) == PictureStatus::Success &&
              picture->attributes().clip_rectangles.has_value() &&
              picture->attributes().clip_rectangles->empty(),
          "an explicit empty clip differs from no clip");
}

}  // namespace

int main() {
  test_canonical_formats();
  test_picture_sources();
  test_atomic_attributes();
  test_atomic_clip_rectangles();
  return 0;
}
