#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/picture.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t minor,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(132);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = 132;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units =
      static_cast<std::uint32_t>(request.bytes.size() / 4U);
  return request;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode RENDER u32");
  return value;
}

void rectangle(x11::ByteWriter& writer, const std::int16_t x,
               const std::int16_t y, const std::uint16_t width,
               const std::uint16_t height) {
  writer.write_u16(std::bit_cast<std::uint16_t>(x));
  writer.write_u16(std::bit_cast<std::uint16_t>(y));
  writer.write_u16(width);
  writer.write_u16(height);
}

DispatchResult create_picture(ServerState& state, const DispatchContext& context,
                              const std::uint32_t picture,
                              const std::uint32_t drawable,
                              const PictureFormatId format) {
  auto writer = header(context.byte_order, 4, 5);
  writer.write_u32(picture);
  writer.write_u32(drawable);
  writer.write_u32(static_cast<std::uint32_t>(format));
  writer.write_u32(0);
  return dispatch_request(state, context, finish(std::move(writer), 4));
}

DispatchResult create_solid(ServerState& state, const DispatchContext& context,
                            const std::uint32_t picture) {
  auto writer = header(context.byte_order, 33, 4);
  writer.write_u32(picture);
  writer.write_u16(0x4040);
  writer.write_u16(0x2020);
  writer.write_u16(0x1010);
  writer.write_u16(0x8080);
  return dispatch_request(state, context, finish(std::move(writer), 33));
}

void test_render(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 91, order, false,
                          InputSnapshot{0, 0, 0, 1, 500}, &extensions, {}};
  constexpr std::uint32_t argb_pixmap = 0x400001;
  constexpr std::uint32_t alpha_pixmap = 0x400002;
  constexpr std::uint32_t destination = 0x400010;
  constexpr std::uint32_t solid = 0x400011;
  constexpr std::uint32_t damage = 0x400020;
  require(state.resources().create_pixmap(
              1, context.resource_base, context.resource_mask, argb_pixmap,
              state.screen().root_window, 32, 2, 2) ==
              CreatePixmapStatus::Success &&
              state.resources().create_pixmap(
                  1, context.resource_base, context.resource_mask, alpha_pixmap,
                  state.screen().root_window, 8, 2, 2) ==
                  CreatePixmapStatus::Success,
          "depth-32 and depth-8 pixmaps are created");
  require(state.resources().find_pixmap(argb_pixmap)->pixels() &&
              state.resources().find_pixmap(alpha_pixmap)->alpha(),
          "extended pixmap depths use canonical storage variants");

  auto writer = header(order, 0, 3);
  writer.write_u32(2);
  writer.write_u32(99);
  auto result = dispatch_request(state, context, finish(std::move(writer), 0));
  require(result.output.size() == 32 && u32(result.output, order, 8) == 0 &&
              u32(result.output, order, 12) == 11,
          "RENDER negotiates exactly 0.11");

  result = dispatch_request(state, context, finish(header(order, 1, 1), 1));
  require(result.output.size() == 196 && u32(result.output, order, 4) == 41 &&
              u32(result.output, order, 8) == 4 &&
              u32(result.output, order, 12) == 1 &&
              u32(result.output, order, 16) == 4 &&
              u32(result.output, order, 32) ==
                  static_cast<std::uint32_t>(PictureFormatId::A1) &&
              u32(result.output, order, 116) ==
                  static_cast<std::uint32_t>(PictureFormatId::Argb32),
          "QueryPictFormats has stable format order and exact tree size");

  writer = header(order, 2, 2);
  writer.write_u32(static_cast<std::uint32_t>(PictureFormatId::A8));
  result = dispatch_request(state, context, finish(std::move(writer), 2));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadMatch),
          "direct formats reject QueryPictIndexValues with BadMatch");
  writer = header(order, 2, 2);
  writer.write_u32(0x12345678);
  result = dispatch_request(state, context, finish(std::move(writer), 2));
  require(result.output[1] == 131 && result.output[10] == 132,
          "unknown format reports RENDER BadPictFormat");

  require(create_picture(state, context, destination, argb_pixmap,
                         PictureFormatId::Argb32)
                  .output.empty() &&
              create_solid(state, context, solid).output.empty(),
          "drawable and solid pictures are created");
  require(state.resources().create_damage(
              1, context.resource_base, context.resource_mask, damage,
              argb_pixmap, DamageReportLevel::BoundingBox) ==
              DamageStatus::Success,
          "create DAMAGE observer for RENDER destination");
  auto* pixels = state.resources().find_pixmap(argb_pixmap)->pixels();
  pixels->at(0, 0) = 0x400A141EU;
  writer = header(order, 8, 9);
  writer.write_u8(3);
  writer.write_padding(3);
  writer.write_u32(solid);
  writer.write_u32(0);
  writer.write_u32(destination);
  writer.write_padding(8);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(1);
  writer.write_u16(1);
  result = dispatch_request(state, context, finish(std::move(writer), 8));
  require(result.output.empty() && pixels->at(0, 0) == 0xA0452A1FU,
          "Composite Over uses exact scalar premultiplied output");
  require(state.resources().find_damage(damage)->accumulated ==
              std::vector<glasswyrm::geometry::Rectangle>{{0, 0, 1, 1}},
          "accepted RENDER mutation produces canonical drawable damage");

  writer = header(order, 6, 5);
  writer.write_u32(destination);
  writer.write_u16(0);
  writer.write_u16(0);
  rectangle(writer, 1, 0, 1, 1);
  require(dispatch_request(state, context, finish(std::move(writer), 6))
              .output.empty(),
          "SetPictureClipRectangles installs a bounded clip");
  std::ranges::fill(pixels->pixels(), 0U);
  writer = header(order, 26, 7);
  writer.write_u8(1);
  writer.write_padding(3);
  writer.write_u32(destination);
  writer.write_u16(0x4040);
  writer.write_u16(0x2020);
  writer.write_u16(0x1010);
  writer.write_u16(0x8080);
  rectangle(writer, 0, 0, 2, 2);
  result = dispatch_request(state, context, finish(std::move(writer), 26));
  require(result.output.empty() && pixels->at(1, 0) == 0x80402010U &&
              pixels->at(0, 0) == 0 && pixels->at(1, 1) == 0,
          "FillRectangles obeys destination picture clipping");

  const auto before = pixels->at(1, 0);
  writer = header(order, 26, 7);
  writer.write_u8(1);
  writer.write_padding(3);
  writer.write_u32(destination);
  writer.write_u16(0xFFFF);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(0);
  rectangle(writer, 0, 0, 1, 1);
  result = dispatch_request(state, context, finish(std::move(writer), 26));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue) &&
              pixels->at(1, 0) == before,
          "invalid premultiplied fill is rejected atomically");

  require(state.resources().invariants_hold(),
          "Picture resources preserve table invariants");
  require(state.resources().free_pixmap(argb_pixmap) ==
              FreePixmapStatus::Success &&
              !state.resources().find_picture(destination),
          "freeing a drawable removes dependent Picture resources");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_render(order);
  return 0;
}
