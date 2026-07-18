#include "glasswyrmd/alpha_storage.hpp"
#include "glasswyrmd/render_ops.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using glasswyrm::geometry::Rectangle;
using glasswyrm::server::AlphaStorage;
using glasswyrm::server::PremultipliedColor;
using glasswyrm::server::RenderDestinationView;
using glasswyrm::server::RenderOperator;
using glasswyrm::server::RenderOpStatus;
using glasswyrm::server::RenderPixelFormat;
using glasswyrm::server::RenderSourceView;
using glasswyrm::server::render_color_from_u16;
using glasswyrm::server::render_composite;
using glasswyrm::server::render_fill;
using gw::test::require;

template <std::size_t Size>
RenderSourceView source_view(const RenderPixelFormat format,
                             const std::uint32_t width,
                             const std::uint32_t height,
                             const std::uint32_t stride,
                             const std::array<std::uint32_t, Size>& pixels) {
  return {format, width, height, stride, std::as_bytes(std::span{pixels})};
}

template <std::size_t Size>
RenderDestinationView destination_view(
    const RenderPixelFormat format, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t stride,
    std::array<std::uint32_t, Size>& pixels) {
  return {format, width, height, stride,
          std::as_writable_bytes(std::span{pixels})};
}

void test_alpha_storage() {
  require(!AlphaStorage::create(0, 1), "zero-width A8 storage is rejected");
  require(!AlphaStorage::create(1, 0), "zero-height A8 storage is rejected");
  require(!AlphaStorage::create(AlphaStorage::kMaximumDimension + 1U, 1),
          "over-wide A8 storage is rejected");
  require(!AlphaStorage::create(AlphaStorage::kMaximumDimension,
                                AlphaStorage::kMaximumDimension),
          "A8 byte budget is enforced");

  auto storage = AlphaStorage::create(4, 3, 7);
  require(storage.has_value(), "bounded A8 storage is created");
  require(storage->stride() == 4 && storage->byte_size() == 12,
          "A8 storage has canonical byte stride");
  storage->fill({-1, 1, 3, 3}, 99);
  require(storage->at(0, 0) == 7 && storage->at(2, 1) == 7,
          "A8 fill clips on the horizontal axis");
  require(storage->at(0, 1) == 99 && storage->at(1, 2) == 99,
          "A8 fill clips and writes its intersection");

  auto resized = storage->resize_preserving_overlap(5, 4, 3);
  require(resized.has_value(), "A8 storage resizes within bounds");
  require(resized->at(0, 1) == 99 && resized->at(3, 0) == 7,
          "A8 resize preserves overlapping pixels");
  require(resized->at(4, 0) == 3 && resized->at(0, 3) == 3,
          "A8 resize initializes new pixels");
}

void test_color_conversion() {
  const auto color = render_color_from_u16(0x4040, 0x2020, 0x1010, 0x8080);
  require(color == PremultipliedColor{64, 32, 16, 128},
          "16-bit RENDER colors narrow deterministically");
  require(!render_color_from_u16(0x8000, 0, 0, 0x4000),
          "non-premultiplied 16-bit colors are rejected");
}

void test_composite_math_and_formats() {
  std::array<std::uint32_t, 1> source{0x80402010U};
  std::array<std::uint32_t, 1> destination{0x400A141EU};
  const auto result = render_composite(
      destination_view(RenderPixelFormat::Argb8888Premultiplied, 1, 1, 4,
                       destination),
      source_view(RenderPixelFormat::Argb8888Premultiplied, 1, 1, 4, source),
      RenderOperator::Over, 0, 0, 0, 0, 1, 1);
  require(result.status == RenderOpStatus::Success,
          "premultiplied Over succeeds");
  require(destination[0] == 0xA0452A1FU,
          "premultiplied Over uses exact rounded scalar math");
  require(result.damage == Rectangle{0, 0, 1, 1},
          "composite reports affected destination bounds");

  destination[0] = 0;
  const auto xrgb_result = render_composite(
      destination_view(RenderPixelFormat::Xrgb8888, 1, 1, 4, destination),
      source_view(RenderPixelFormat::Argb8888Premultiplied, 1, 1, 4, source),
      RenderOperator::Src, 0, 0, 0, 0, 1, 1);
  require(xrgb_result.status == RenderOpStatus::Success,
          "ARGB source copies to XRGB");
  require(destination[0] == 0xFF402010U,
          "XRGB writes always force the output alpha opaque");
}

void test_composite_validation_and_aliasing() {
  std::array<std::uint32_t, 1> invalid_source{0x10200000U};
  std::array<std::uint32_t, 1> destination{0xFF010203U};
  const auto invalid_result = render_composite(
      destination_view(RenderPixelFormat::Xrgb8888, 1, 1, 4, destination),
      source_view(RenderPixelFormat::Argb8888Premultiplied, 1, 1, 4,
                  invalid_source),
      RenderOperator::Src, 0, 0, 0, 0, 1, 1);
  require(invalid_result.status == RenderOpStatus::InvalidPremultipliedPixel,
          "malformed premultiplied source pixels are rejected");
  require(destination[0] == 0xFF010203U,
          "validation failure leaves the destination unchanged");

  std::array<std::uint32_t, 3> aliased{0xFF000001U, 0xFF000002U,
                                       0xFF000003U};
  const RenderSourceView alias_source{
      RenderPixelFormat::Xrgb8888, 3, 1, 12,
      std::as_bytes(std::span<const std::uint32_t>{aliased})};
  const auto alias_result = render_composite(
      destination_view(RenderPixelFormat::Xrgb8888, 3, 1, 12, aliased),
      alias_source, RenderOperator::Src, 0, 0, 1, 0, 2, 1);
  require(alias_result.status == RenderOpStatus::Success,
          "self-aliased composite succeeds");
  require(aliased == std::array<std::uint32_t, 3>{0xFF000001U, 0xFF000001U,
                                                  0xFF000002U},
          "self-aliased composite snapshots source pixels");

  std::array<std::byte, 3> too_short{};
  const RenderDestinationView invalid_destination{
      RenderPixelFormat::Xrgb8888, 1, 1, 3, too_short};
  const auto surface_result = render_composite(
      invalid_destination,
      source_view(RenderPixelFormat::Xrgb8888, 1, 1, 4, invalid_source),
      RenderOperator::Src, 0, 0, 0, 0, 1, 1);
  require(surface_result.status == RenderOpStatus::InvalidSurface,
          "undersized surfaces are rejected");
}

void test_destination_clipping() {
  std::array<std::uint32_t, 3> source{0xFF010101U, 0xFF020202U,
                                      0xFF030303U};
  std::array<std::uint32_t, 3> destination{};
  constexpr std::array<Rectangle, 1> clip{{{1, 0, 1, 1}}};
  const auto result = render_composite(
      destination_view(RenderPixelFormat::Xrgb8888, 3, 1, 12, destination),
      source_view(RenderPixelFormat::Xrgb8888, 3, 1, 12, source),
      RenderOperator::Src, 0, 0, 0, 0, 3, 1, clip);
  require(result.status == RenderOpStatus::Success,
          "destination-clipped composite succeeds");
  require(destination ==
              std::array<std::uint32_t, 3>{0, 0xFF020202U, 0},
          "destination clip limits composite writes");
  require(result.damage == Rectangle{1, 0, 1, 1},
          "destination clip limits reported damage");
}

void test_fill_formats_and_clipping() {
  std::array<std::uint32_t, 4> argb{};
  constexpr std::array<Rectangle, 1> all{{{0, 0, 2, 2}}};
  constexpr std::array<Rectangle, 1> clip{{{1, 1, 1, 1}}};
  const auto result = render_fill(
      destination_view(RenderPixelFormat::Argb8888Premultiplied, 2, 2, 8,
                       argb),
      RenderOperator::Over, {64, 32, 16, 128}, all, clip);
  require(result.status == RenderOpStatus::Success,
          "premultiplied clipped fill succeeds");
  require(argb == std::array<std::uint32_t, 4>{0, 0, 0, 0x80402010U},
          "fill applies color only inside the destination clip");
  require(result.damage == Rectangle{1, 1, 1, 1},
          "fill reports clipped damage");

  std::array<std::uint32_t, 1> xrgb{0x00112233U};
  const auto xrgb_result = render_fill(
      destination_view(RenderPixelFormat::Xrgb8888, 1, 1, 4, xrgb),
      RenderOperator::Src, {0, 0, 0, 0},
      std::array<Rectangle, 1>{{{0, 0, 1, 1}}});
  require(xrgb_result.status == RenderOpStatus::Success &&
              xrgb[0] == 0xFF000000U,
          "XRGB fill forces an opaque output alpha");

  std::array<std::byte, 2> alpha{};
  const RenderDestinationView a8{RenderPixelFormat::A8, 2, 1, 2, alpha};
  require(render_fill(a8, RenderOperator::Src, {0, 0, 0, 127}, all).status ==
              RenderOpStatus::Success &&
              std::to_integer<std::uint8_t>(alpha[0]) == 127,
          "A8 fill stores the scalar alpha channel");
  const RenderDestinationView a1{RenderPixelFormat::A1, 2, 1, 2, alpha};
  require(render_fill(a1, RenderOperator::Src, {0, 0, 0, 128}, all).status ==
              RenderOpStatus::Success &&
              alpha[0] == std::byte{1},
          "A1 fill deterministically thresholds alpha");
}

}  // namespace

int main() {
  test_alpha_storage();
  test_color_conversion();
  test_composite_math_and_formats();
  test_composite_validation_and_aliasing();
  test_destination_clipping();
  test_fill_formats_and_clipping();
  return 0;
}
