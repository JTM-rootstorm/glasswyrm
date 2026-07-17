#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t minor, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t opcode,
                          const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = opcode;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units = request.bytes.size() / 4U;
  return request;
}

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read RANDR config u16");
  return value;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read RANDR config u32");
  return value;
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 16;
  spec.height = 16;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create RANDR selection window");
}

DispatchResult select_input(ServerState& state, const DispatchContext& context,
                            const std::uint32_t window,
                            const std::uint16_t mask) {
  auto writer = header(context.byte_order, 134, 4, 3);
  writer.write_u32(window);
  writer.write_u16(mask);
  writer.write_u16(0);
  return dispatch_request(state, context,
                          finish(std::move(writer), 134, 4));
}

DispatchResult set_crtc(ServerState& state, const DispatchContext& context,
                        const std::int16_t x, const std::uint32_t mode,
                        const std::uint16_t rotation, const bool output) {
  auto writer = header(context.byte_order, 134, 21, output ? 8 : 7);
  writer.write_u32(kRandRCrtcId);
  writer.write_u32(kRandRConfigurationTimestamp);
  writer.write_u32(kRandRConfigurationTimestamp);
  writer.write_u16(static_cast<std::uint16_t>(x));
  writer.write_u16(0);
  writer.write_u32(mode);
  writer.write_u16(rotation);
  writer.write_u16(0);
  if (output) writer.write_u32(kRandROutputId);
  return dispatch_request(state, context,
                          finish(std::move(writer), 134, 21));
}

void test_properties_and_crtc(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 77, order, false, {},
                          &extensions};

  auto writer = header(order, 134, 10, 2);
  writer.write_u32(kRandROutputId);
  auto result = dispatch_request(state, context,
                                 finish(std::move(writer), 134, 10));
  require(result.output.size() == 32 && u16(result.output, order, 8) == 0,
          "ListOutputProperties reports no properties");

  writer = header(order, 134, 11, 3);
  writer.write_u32(kRandROutputId);
  writer.write_u32(1);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 11));
  require(result.output.size() == 32 && result.output[8] == 0 &&
              result.output[9] == 0 && result.output[10] == 0,
          "QueryOutputProperty gives deterministic empty metadata");

  writer = header(order, 134, 15, 7);
  writer.write_u32(kRandROutputId);
  writer.write_u32(1);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(64);
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u16(0);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 15));
  require(result.output.size() == 32 && result.output[1] == 0 &&
              u32(result.output, order, 8) == 0 &&
              u32(result.output, order, 16) == 0,
          "GetOutputProperty returns an empty value");

  writer = header(order, 134, 11, 3);
  writer.write_u32(kRandROutputId);
  writer.write_u32(0xf0000000);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 11));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAtom),
          "output property requests validate atoms");

  writer = header(order, 134, 20, 3);
  writer.write_u32(kRandRCrtcId);
  writer.write_u32(kRandRConfigurationTimestamp);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 20));
  require(result.output.size() == 40 && result.output[1] == 0 &&
              u16(result.output, order, 16) == state.screen().width_pixels &&
              u32(result.output, order, 20) == kRandRModeId &&
              u16(result.output, order, 28) == 1 &&
              u32(result.output, order, 32) == kRandROutputId &&
              u32(result.output, order, 36) == kRandROutputId,
          "GetCrtcInfo reports current and possible output sets");

  result = set_crtc(state, context, 0, kRandRModeId, kRandRRotate0, true);
  require(result.output.size() == 32 && result.output[1] == 0 &&
              u32(result.output, order, 8) == kRandRConfigurationTimestamp,
          "SetCrtcConfig accepts the exact idempotent configuration");
  result = set_crtc(state, context, 1, kRandRModeId, kRandRRotate0, true);
  require(result.output[1] == 3,
          "SetCrtcConfig rejects a position change with Failed");
  result = set_crtc(state, context, 0, 0, kRandRRotate0, false);
  require(result.output[1] == 3,
          "SetCrtcConfig rejects disabling the current topology");
  result = set_crtc(state, context, 0, 0xabcdef01, kRandRRotate0, true);
  require(result.output[1] == 138 && u16(result.output, order, 8) == 21,
          "SetCrtcConfig reports BadMode for an unknown mode object");

  writer = header(order, 134, 22, 2);
  writer.write_u32(kRandRCrtcId);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 22));
  require(result.output.size() == 32 && u16(result.output, order, 8) == 0,
          "GetCrtcGammaSize reports the deferred zero-size table");
  writer = header(order, 134, 22, 2);
  writer.write_u32(0xabcdef02);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 134, 22));
  require(result.output[1] == 137 && result.output[10] == 134,
          "invalid CRTC uses the checked RANDR BadCrtc range");
}

void test_selection_cleanup(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext first{1, 0x400000, 0x1fffff, 1, order, false, {},
                        &extensions};
  DispatchContext second = first;
  second.client_id = 2;
  constexpr std::uint32_t window = 0x400010;
  create_window(state, 1, window);
  require(select_input(state, first, state.screen().root_window, 0xf)
                  .output.empty() &&
              select_input(state, first, state.screen().root_window, 1)
                  .output.empty() &&
              state.randr().selection(1, state.screen().root_window) == 1,
          "SelectInput replaces a client's per-window mask");
  require(select_input(state, second, window, 2).output.empty() &&
              state.randr().selection(2, window) == 2,
          "SelectInput tracks independent clients and windows");

  auto writer = header(order, 4, 0, 2);
  writer.write_u32(window);
  const auto destroyed = dispatch_request(
      state, first, finish(std::move(writer), 4, 0));
  require(destroyed.output.empty() && state.randr().selection(2, window) == 0,
          "window destruction removes RANDR selections");

  require(select_input(state, first, state.screen().root_window, 0).output.empty() &&
              state.randr().selection(1, state.screen().root_window) == 0,
          "a zero SelectInput mask unsubscribes");
  require(select_input(state, second, state.screen().root_window, 0x10)
                  .output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "SelectInput rejects post-1.3 masks");
  require(select_input(state, second, 0x400099, 1).output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadWindow),
          "SelectInput validates selected windows");

  require(select_input(state, second, state.screen().root_window, 1)
                  .output.empty(),
          "create disconnect-cleanup subscription");
  (void)state.cleanup_client(2);
  require(state.randr().selections().empty(),
          "client cleanup removes RANDR subscriptions");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_properties_and_crtc(order);
    test_selection_cleanup(order);
  }
  return 0;
}
