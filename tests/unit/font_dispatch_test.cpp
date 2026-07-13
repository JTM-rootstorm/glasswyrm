#include "glasswyrmd/font.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <span>
#include <string_view>

namespace x11 = gw::protocol::x11;
using namespace glasswyrm::server;

namespace {
x11::FramedRequest finish(x11::ByteWriter writer, const x11::CoreOpcode opcode,
                          const std::uint8_t data = 0) {
  x11::FramedRequest result;
  result.opcode = static_cast<std::uint8_t>(opcode);
  result.data = data;
  result.bytes = std::move(writer).take();
  result.length_units = static_cast<std::uint16_t>(result.bytes.size() / 4U);
  return result;
}

x11::ByteWriter header(const x11::ByteOrder order,
                       const x11::CoreOpcode opcode,
                       const std::uint8_t data,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}
}  // namespace

int main() {
  gw::test::require(matches_fixed_font("fixed") && matches_fixed_font("6X13") &&
                    matches_fixed_font("-misc-fixed-medium-r-*"),
                    "fixed aliases");
  gw::test::require(!matches_fixed_font("cursor"), "unknown alias");
  gw::test::require(fixed_glyph('0') != fixed_glyph('1') &&
                    fixed_glyph('a') == fixed_glyph('A') &&
                    fixed_glyph(1) == fixed_glyph(255),
                    "deterministic glyph profile");

  constexpr std::uint32_t base = 0x00400000U;
  constexpr std::uint32_t mask = 0x001fffffU;
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    ServerState state;
    DispatchContext context{1, base, mask, 0x12345, order};
    auto open = header(order, x11::CoreOpcode::OpenFont, 0, 5);
    open.write_u32(base + 1); open.write_u16(5); open.write_u16(0);
    open.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>("fixed"), 5));
    open.write_padding(3);
    auto result = dispatch_request(state, context,
        finish(std::move(open), x11::CoreOpcode::OpenFont));
    gw::test::require(result.output.empty() && state.resources().find_font(base + 1),
                      "OpenFont");

    auto query = header(order, x11::CoreOpcode::QueryFont, 0, 2);
    query.write_u32(base + 1);
    result = dispatch_request(state, context,
        finish(std::move(query), x11::CoreOpcode::QueryFont));
    gw::test::require(result.output.size() == 60U + 95U * 12U &&
                      result.output[0] == 1, "QueryFont reply");

    auto extents = header(order, x11::CoreOpcode::QueryTextExtents, 0, 3);
    extents.write_u32(base + 1); extents.write_u8(0); extents.write_u8('0');
    extents.write_u8(0); extents.write_u8(':');
    result = dispatch_request(state, context,
        finish(std::move(extents), x11::CoreOpcode::QueryTextExtents));
    x11::ByteReader extents_reply(
        std::span<const std::uint8_t>(result.output).subspan(16), order);
    std::uint32_t extent_width{}; (void)extents_reply.read_u32(extent_width);
    gw::test::require(result.output.size() == 32 && extent_width == 12,
                      "QueryTextExtents fixed advance");

    auto list = header(order, x11::CoreOpcode::ListFonts, 0, 4);
    list.write_u16(1); list.write_u16(5);
    list.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>("fixed"), 5));
    list.write_padding(3);
    result = dispatch_request(state, context,
        finish(std::move(list), x11::CoreOpcode::ListFonts));
    gw::test::require(result.output.size() == 40 && result.output[32] == 5 &&
                      std::string_view(reinterpret_cast<const char*>(result.output.data() + 33), 5) == "fixed",
                      "ListFonts fixed result");

    auto pixmap = header(order, x11::CoreOpcode::CreatePixmap, 24, 4);
    pixmap.write_u32(base + 2); pixmap.write_u32(state.screen().root_window);
    pixmap.write_u16(24); pixmap.write_u16(16);
    result = dispatch_request(state, context,
        finish(std::move(pixmap), x11::CoreOpcode::CreatePixmap, 24));
    gw::test::require(result.output.empty(), "CreatePixmap");
    auto gc = header(order, x11::CoreOpcode::CreateGC, 0, 7);
    gc.write_u32(base + 3); gc.write_u32(base + 2);
    gc.write_u32((1U << 2U) | (1U << 3U) | (1U << 14U));
    gc.write_u32(0xffffff); gc.write_u32(0x102030); gc.write_u32(base + 1);
    result = dispatch_request(state, context,
        finish(std::move(gc), x11::CoreOpcode::CreateGC));
    gw::test::require(result.output.empty(), "GCFont accepts open font");

    auto image = header(order, x11::CoreOpcode::ImageText8, 2, 5);
    image.write_u32(base + 2); image.write_u32(base + 3);
    image.write_u16(1); image.write_u16(11); image.write_u8('0'); image.write_u8(':');
    image.write_padding(2);
    result = dispatch_request(state, context,
        finish(std::move(image), x11::CoreOpcode::ImageText8, 2));
    gw::test::require(result.output.empty() &&
                      std::get<std::shared_ptr<PixelStorage>>(
                          state.resources().find_pixmap(base + 2)->storage)->at(1, 1) ==
                          0xff102030U,
                      "ImageText8 background");

    auto poly = header(order, x11::CoreOpcode::PolyText8, 0, 5);
    poly.write_u32(base + 2); poly.write_u32(base + 3);
    poly.write_u16(1); poly.write_u16(11);
    poly.write_u8(2); poly.write_u8(1); poly.write_u8('0'); poly.write_u8(':');
    result = dispatch_request(state, context,
        finish(std::move(poly), x11::CoreOpcode::PolyText8));
    gw::test::require(result.output.empty() &&
                      std::get<std::shared_ptr<PixelStorage>>(
                          state.resources().find_pixmap(base + 2)->storage)->at(3, 3) ==
                          0xffffffffU,
                      "PolyText8 signed delta and glyph foreground");

    auto close = header(order, x11::CoreOpcode::CloseFont, 0, 2);
    close.write_u32(base + 1);
    result = dispatch_request(state, context,
        finish(std::move(close), x11::CoreOpcode::CloseFont));
    gw::test::require(result.output.empty() &&
                      state.resources().find_gc(base + 3)->font == kDefaultFontXid,
                      "font selection survives CloseFont");
  }
}
