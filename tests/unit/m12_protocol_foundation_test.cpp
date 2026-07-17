#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t opcode,
                          const std::uint8_t data = 0) {
  auto bytes = std::move(writer).take();
  x11::FramedRequest request;
  request.opcode = opcode;
  request.data = data;
  request.length_units = static_cast<std::uint32_t>(bytes.size() / 4);
  request.bytes = std::move(bytes);
  return request;
}

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t data, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish_big(x11::ByteWriter writer,
                              const std::uint8_t opcode,
                              const std::uint8_t data) {
  auto request = finish(std::move(writer), opcode, data);
  request.header_size = x11::kBigRequestHeaderSize;
  return request;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read u32");
  return value;
}

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read u16");
  return value;
}

x11::FramedRequest query_extension(const x11::ByteOrder order,
                                   const std::string_view name) {
  const auto padded = (name.size() + 3U) & ~std::size_t{3U};
  auto writer = header(order, 98, 0,
                       static_cast<std::uint16_t>(2 + padded / 4));
  writer.write_u16(static_cast<std::uint16_t>(name.size()));
  writer.write_u16(0);
  writer.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
  writer.write_padding(padded - name.size());
  return finish(std::move(writer), 98);
}

void test_registry(const x11::ByteOrder order) {
  const std::vector<std::string> disabled{"MIT-SHM"};
  const ExtensionRegistry extensions(true, disabled);
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 7, order, false, {},
                          &extensions};

  auto result = dispatch_request(state, context,
                                 query_extension(order, "BIG-REQUESTS"));
  require(result.output.size() == 32 && result.output[8] == 1 &&
              result.output[9] == 128 && result.output[10] == 0 &&
              result.output[11] == 0,
          "QueryExtension returns the checked BIG-REQUESTS assignment");
  result = dispatch_request(state, context, query_extension(order, "MIT-SHM"));
  require(result.output[8] == 0 && result.output[9] == 0,
          "disabled extension is absent");
  result = dispatch_request(state, context, query_extension(order, "render"));
  require(result.output[8] == 0, "extension names remain case-sensitive");

  result = dispatch_request(
      state, context, finish(header(order, 99, 0, 1), 99));
  require(result.output[1] == 6 && u32(result.output, order, 4) != 0,
          "ListExtensions contains all enabled names");
  std::size_t offset = 32;
  for (const auto& extension : kExtensionRegistry) {
    if (extension.name == "MIT-SHM") continue;
    require(offset < result.output.size() &&
                result.output[offset] == extension.name.size(),
            "ListExtensions retains registry order");
    ++offset;
    const std::string_view encoded(
        reinterpret_cast<const char*>(result.output.data() + offset),
        extension.name.size());
    require(encoded == extension.name, "ListExtensions name is exact");
    offset += extension.name.size();
  }

  result = dispatch_request(
      state, context, finish(header(order, 128, 0, 1), 128));
  require(result.enable_big_requests && result.output.size() == 32 &&
              u32(result.output, order, 8) ==
                  x11::kMaximumBigRequestLengthUnits,
          "BIG-REQUESTS Enable advertises the enforced cap");
  result = dispatch_request(
      state, context, finish(header(order, 129, 7, 1), 129, 7));
  require(result.output[0] == 0 &&
              result.output[1] ==
                  static_cast<std::uint8_t>(x11::CoreErrorCode::BadRequest) &&
              u16(result.output, order, 8) == 7 && result.output[10] == 129,
          "disabled extension opcode reports request major and minor");
}

void test_extension_wire(const x11::ByteOrder order) {
  const auto* xfixes = find_extension("XFIXES");
  require(xfixes != nullptr, "XFIXES registry entry exists");
  const auto packet = encode_extension_error(order, *xfixes, 0, 0x12345,
                                             0xabcdef, 130, 7);
  require(packet && packet->size() == 32 && (*packet)[1] == 129 &&
              u16(*packet, order, 2) == 0x2345 &&
              u32(*packet, order, 4) == 0xabcdef &&
              u16(*packet, order, 8) == 7 && (*packet)[10] == 130,
          "extension error encodes absolute code and request metadata");
  const std::array<std::uint8_t, 4> body{1, 2, 3, 4};
  const auto event =
      encode_extension_event(order, *xfixes, 0, 0x10002, 9, body, true);
  require(event && event->size() == 32 && (*event)[0] == (65U | 0x80U) &&
              (*event)[1] == 9 && u16(*event, order, 2) == 2 &&
              (*event)[4] == 1,
          "extension event encodes SendEvent and recipient byte order");
  require(!encode_extension_error(order, *xfixes, 1, 1, 0, 130, 0) &&
              !encode_extension_event(order, *xfixes, 1, 1, 0, {}, false),
          "relative extension ranges are bounded");
}

void test_colormaps(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 9, order, false, {},
                          &extensions};
  constexpr std::uint32_t xid = 0x400123;
  auto writer = header(order, 78, 0, 4);
  writer.write_u32(xid);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(state.screen().root_visual);
  auto result = dispatch_request(state, context, finish(std::move(writer), 78));
  require(result.output.empty() && state.resources().valid_colormap(xid),
          "CreateColormap adds an AllocNone root-visual resource");

  writer = header(order, 2, 0, 4);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(1U << 13U);
  writer.write_u32(xid);
  result = dispatch_request(state, context, finish(std::move(writer), 2));
  require(result.output.empty() &&
              state.resources()
                      .find_window(state.screen().root_window)
                      ->attributes.colormap == xid,
          "CWColormap accepts a client colormap resource");

  writer = header(order, 84, 0, 4);
  writer.write_u32(xid);
  writer.write_u16(0xffff);
  writer.write_u16(0x8000);
  writer.write_u16(0);
  writer.write_u16(0);
  result = dispatch_request(state, context, finish(std::move(writer), 84));
  require(result.output.size() == 32 && u32(result.output, order, 16) == 0xff8000,
          "TrueColor allocation accepts a client colormap");

  writer = header(order, 83, 0, 2);
  writer.write_u32(state.screen().root_window);
  result = dispatch_request(state, context, finish(std::move(writer), 83));
  require(result.output.size() == 36 &&
              u32(result.output, order, 32) == state.screen().default_colormap,
          "ListInstalledColormaps reports the synthetic default");

  writer = header(order, 2, 0, 4);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(1U << 13U);
  writer.write_u32(state.screen().default_colormap);
  result = dispatch_request(state, context, finish(std::move(writer), 2));
  require(result.output.empty(), "CWColormap restores the default colormap");

  writer = header(order, 79, 0, 2);
  writer.write_u32(xid);
  result = dispatch_request(state, context, finish(std::move(writer), 79));
  require(result.output.empty() && !state.resources().valid_colormap(xid),
          "FreeColormap releases the client resource");

  writer = header(order, 79, 0, 2);
  writer.write_u32(state.screen().default_colormap);
  result = dispatch_request(state, context, finish(std::move(writer), 79));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess),
          "the default colormap cannot be freed");

  writer = header(order, 78, 1, 4);
  writer.write_u32(xid);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(state.screen().root_visual);
  result = dispatch_request(state, context, finish(std::move(writer), 78, 1));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "AllocAll colormaps are rejected");

  const ExtensionRegistry historical;
  context.extensions = &historical;
  writer = header(order, 78, 0, 4);
  writer.write_u32(xid);
  writer.write_u32(state.screen().root_window);
  writer.write_u32(state.screen().root_visual);
  result = dispatch_request(state, context, finish(std::move(writer), 78));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadRequest),
          "historical profile keeps new colormap requests unavailable");
}

void test_extended_core_dispatch(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 12, order, false, {},
                          &extensions};
  constexpr std::uint32_t pixmap = 0x400210;
  constexpr std::uint32_t gc = 0x400211;
  require(state.resources().create_pixmap(
              1, context.resource_base, context.resource_mask, pixmap,
              state.screen().root_window, 24, 1, 1) ==
              CreatePixmapStatus::Success &&
              state.resources().create_gc(1, context.resource_base,
                                          context.resource_mask, gc, pixmap,
                                          {}) == CreateGcStatus::Success,
          "extended PutImage fixture resources are created");
  x11::ByteWriter writer(order);
  writer.write_u8(72);
  writer.write_u8(2);
  writer.write_u16(0);
  writer.write_u32(8);  // complete extended request, in four-byte units
  writer.write_u32(pixmap);
  writer.write_u32(gc);
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u8(0);
  writer.write_u8(24);
  writer.write_u16(0);
  const std::array<std::uint8_t, 4> pixel{0x56, 0x34, 0x12, 0x00};
  writer.write_bytes(pixel);
  const auto result = dispatch_request(
      state, context, finish_big(std::move(writer), 72, 2));
  const auto* pixels = state.resources().find_pixmap(pixmap)->pixels();
  require(result.output.empty() && pixels && pixels->at(0, 0) == 0xff123456,
          "extended core framing hides the extra length word from PutImage");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_registry(order);
    test_extension_wire(order);
    test_colormaps(order);
    test_extended_core_dispatch(order);
  }
  return 0;
}
