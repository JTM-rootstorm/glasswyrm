#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>
#include <string_view>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t minor,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(134);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = 134;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units = request.bytes.size() / 4U;
  return request;
}

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read RANDR u16");
  return value;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read RANDR u32");
  return value;
}

DispatchResult window_request(ServerState& state,
                              const DispatchContext& context,
                              const std::uint8_t minor) {
  auto writer = header(context.byte_order, minor, 2);
  writer.write_u32(state.screen().root_window);
  return dispatch_request(state, context, finish(std::move(writer), minor));
}

void test_reporting(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 0x12345, order, false, {},
                          &extensions};

  auto writer = header(order, 0, 3);
  writer.write_u32(9);
  writer.write_u32(9);
  auto result = dispatch_request(state, context,
                                 finish(std::move(writer), 0));
  require(result.output.size() == 32 && u32(result.output, order, 8) == 1 &&
              u32(result.output, order, 12) == 3,
          "RANDR advertises exactly version 1.3");

  result = window_request(state, context, 5);
  require(result.output.size() == 44 && result.output[1] == kRandRRotate0 &&
              u32(result.output, order, 8) == state.screen().root_window &&
              u16(result.output, order, 20) == 1 &&
              u16(result.output, order, 32) == state.screen().width_pixels &&
              u16(result.output, order, 40) == 1,
          "GetScreenInfo reports one current size and refresh rate");

  result = window_request(state, context, 6);
  require(result.output.size() == 32 &&
              u16(result.output, order, 8) == state.screen().width_pixels &&
              u16(result.output, order, 12) == state.screen().width_pixels &&
              u16(result.output, order, 14) == state.screen().height_pixels,
          "GetScreenSizeRange pins the current dimensions");

  for (const std::uint8_t minor : {8, 25}) {
    result = window_request(state, context, minor);
    require(result.output.size() == 80 && u16(result.output, order, 16) == 1 &&
                u16(result.output, order, 18) == 1 &&
                u16(result.output, order, 20) == 1 &&
                u32(result.output, order, 32) == kRandRCrtcId &&
                u32(result.output, order, 36) == kRandROutputId &&
                u32(result.output, order, 40) == kRandRModeId &&
                u16(result.output, order, 44) == state.screen().width_pixels &&
                std::string_view(
                    reinterpret_cast<const char*>(result.output.data() + 72),
                    u16(result.output, order, 22)) == "1024x768",
            "screen resources expose the stable one-output topology");
  }

  writer = header(order, 9, 3);
  writer.write_u32(kRandROutputId);
  writer.write_u32(kRandRConfigurationTimestamp);
  result = dispatch_request(state, context, finish(std::move(writer), 9));
  require(result.output.size() == 56 && u32(result.output, order, 4) == 6 &&
              u32(result.output, order, 12) == kRandRCrtcId &&
              result.output[24] == 0 && u16(result.output, order, 26) == 1 &&
              u16(result.output, order, 30) == 1 &&
              u32(result.output, order, 36) == kRandRCrtcId &&
              u32(result.output, order, 40) == kRandRModeId &&
              std::string_view(
                  reinterpret_cast<const char*>(result.output.data() + 44),
                  11) == "Glasswyrm-1",
          "GetOutputInfo uses the protocol's 36-byte fixed reply header");

  result = window_request(state, context, 31);
  require(result.output.size() == 32 &&
              u32(result.output, order, 8) == kRandROutputId,
          "GetOutputPrimary returns the only output");

  writer = header(order, 9, 3);
  writer.write_u32(0xdeadbeef);
  writer.write_u32(0);
  result = dispatch_request(state, context, finish(std::move(writer), 9));
  require(result.output.size() == 32 && result.output[1] == 136 &&
              u16(result.output, order, 8) == 9 && result.output[10] == 134 &&
              u32(result.output, order, 4) == 0xdeadbeef,
          "invalid output uses the checked RANDR BadOutput range");

  writer = header(order, 5, 2);
  writer.write_u32(0x400099);
  result = dispatch_request(state, context, finish(std::move(writer), 5));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadWindow),
          "screen reporting validates request windows");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_reporting(order);
  return 0;
}
