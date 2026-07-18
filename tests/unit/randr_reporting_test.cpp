#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/output_configuration_events.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "output/model/layout.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>
#include <string>
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

glasswyrm::output::OutputLayout multi_output_layout() {
  using namespace glasswyrm::output;
  constexpr OutputId left{10};
  constexpr OutputId right{20};
  constexpr OutputModeId left_mode{11};
  constexpr OutputModeId right_mode{21};
  OutputLayout layout;
  const auto add = [&](const OutputId id, const OutputModeId mode_id,
                       std::string name, const std::uint32_t width,
                       const std::uint32_t height, const std::int32_t x,
                       const RationalScale scale, const bool primary) {
    OutputDescriptor descriptor;
    descriptor.id = id;
    descriptor.name = std::move(name);
    descriptor.connected = true;
    descriptor.physical_width_mm = width / 4;
    descriptor.physical_height_mm = height / 4;
    descriptor.mode_configurable = true;
    descriptor.scale_configurable = true;
    descriptor.transform_configurable = true;
    descriptor.primary_eligible = true;
    descriptor.arbitrary_headless_mode = true;
    descriptor.supported_transform_mask = kAllOutputTransformsMask;
    descriptor.modes.push_back({mode_id, id, width, height, 60'000, 0,
                                std::to_string(width) + "x" +
                                    std::to_string(height),
                                true, true});
    layout.descriptors.emplace(id, std::move(descriptor));
    OutputState state;
    state.output_id = id;
    state.enabled = true;
    state.mode_id = mode_id;
    state.logical_x = x;
    state.logical_width = 640;
    state.logical_height = 480;
    state.physical_width = width;
    state.physical_height = height;
    state.refresh_millihertz = 60'000;
    state.scale = scale;
    state.primary = primary;
    state.generation = 9;
    layout.states.emplace(id, state);
  };
  add(left, left_mode, "LEFT", 640, 480, 0, {1, 1}, true);
  add(right, right_mode, "RIGHT", 800, 600, 640, {5, 4}, false);
  layout.primary_output_id = left;
  layout.root_logical_width = 1280;
  layout.root_logical_height = 480;
  layout.generation = 9;
  layout.enabled_output_count = 2;
  layout.output_order = {left, right};
  return layout;
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

void test_multi_output_reporting(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  auto layout = multi_output_layout();
  auto screen = state.screen();
  screen.width_pixels = 1280;
  screen.height_pixels = 480;
  require(state.update_screen_geometry(screen) &&
              state.randr().configure_output_layout(layout),
          "install the validated RANDR multi-output topology");
  DispatchContext context{1, 0x400000, 0x1fffff, 0x4321, order, false, {},
                          &extensions};

  auto result = window_request(state, context, 25);
  require(result.output.size() == 128 &&
              u32(result.output, order, 4) == 24 &&
              u32(result.output, order, 8) == 9 &&
              u32(result.output, order, 12) == 9 &&
              u16(result.output, order, 16) == 2 &&
              u16(result.output, order, 18) == 2 &&
              u16(result.output, order, 20) == 2 &&
              u16(result.output, order, 22) == 14 &&
              u32(result.output, order, 32) == 0x101 &&
              u32(result.output, order, 36) == 0x104 &&
              u32(result.output, order, 40) == 0x100 &&
              u32(result.output, order, 44) == 0x103 &&
              u32(result.output, order, 48) == 0x102 &&
              u32(result.output, order, 80) == 0x105 &&
              std::string_view(
                  reinterpret_cast<const char*>(result.output.data() + 112),
                  14) == "640x480800x600",
          "screen resources expose stable sorted multi-output objects");

  auto writer = header(order, 9, 3);
  writer.write_u32(0x103);
  writer.write_u32(9);
  result = dispatch_request(state, context, finish(std::move(writer), 9));
  require(result.output.size() == 52 && u32(result.output, order, 4) == 5 &&
              u32(result.output, order, 8) == 9 &&
              u32(result.output, order, 12) == 0x104 &&
              result.output[24] == 0 && u16(result.output, order, 26) == 1 &&
              u16(result.output, order, 28) == 1 &&
              u16(result.output, order, 30) == 1 &&
              u16(result.output, order, 34) == 5 &&
              u32(result.output, order, 36) == 0x104 &&
              u32(result.output, order, 40) == 0x105 &&
              std::string_view(
                  reinterpret_cast<const char*>(result.output.data() + 44),
                  5) == "RIGHT",
          "GetOutputInfo reports the selected enabled output");

  writer = header(order, 20, 3);
  writer.write_u32(0x104);
  writer.write_u32(9);
  result = dispatch_request(state, context, finish(std::move(writer), 20));
  require(result.output.size() == 40 && u32(result.output, order, 4) == 2 &&
              u16(result.output, order, 12) == 640 &&
              u16(result.output, order, 14) == 0 &&
              u16(result.output, order, 16) == 640 &&
              u16(result.output, order, 18) == 480 &&
              u32(result.output, order, 20) == 0x105 &&
              u16(result.output, order, 24) == kRandRRotate0 &&
              u16(result.output, order, 26) == 0x3f &&
              u32(result.output, order, 32) == 0x103,
          "GetCrtcInfo exposes logical placement and current mode");

  writer = header(order, 21, 8);
  writer.write_u32(0x104);
  writer.write_u32(9);
  writer.write_u32(9);
  writer.write_u16(640);
  writer.write_u16(0);
  writer.write_u32(0x105);
  writer.write_u16(kRandRRotate0);
  writer.write_u16(0);
  writer.write_u32(0x103);
  result = dispatch_request(state, context, finish(std::move(writer), 21));
  require(result.output.size() == 32 && result.output[1] == 0 &&
              u32(result.output, order, 8) == 9,
          "SetCrtcConfig accepts only an exact restatement");

  writer = header(order, 21, 8);
  writer.write_u32(0x104);
  writer.write_u32(9);
  writer.write_u32(9);
  writer.write_u16(641);
  writer.write_u16(0);
  writer.write_u32(0x105);
  writer.write_u16(kRandRRotate0);
  writer.write_u16(0);
  writer.write_u32(0x103);
  result = dispatch_request(state, context, finish(std::move(writer), 21));
  require(result.output[1] == 3,
          "SetCrtcConfig rejects a layout mutation without applying it");

  result = window_request(state, context, 31);
  require(u32(result.output, order, 8) == 0x100,
          "GetOutputPrimary maps the internal primary to a stable XID");

  layout.generation = 10;
  for (auto& [id, output] : layout.states) {
    static_cast<void>(id);
    output.generation = 10;
  }
  require(state.randr().configure_output_layout(layout) &&
              state.randr().outputs()[0].xid == 0x100 &&
              state.randr().outputs()[1].xid == 0x103 &&
              state.randr().configuration_timestamp() == 10,
          "layout updates retain all live RANDR object handles");

  layout.states.at(glasswyrm::output::OutputId{20}).transform =
      glasswyrm::output::OutputTransform::Flipped;
  layout.generation = 11;
  for (auto& [id, output] : layout.states) {
    static_cast<void>(id);
    output.generation = 11;
  }
  require(state.randr().configure_output_layout(layout),
          "apply a representable output transform to RANDR state");
  writer = header(order, 20, 3);
  writer.write_u32(0x104);
  writer.write_u32(11);
  result = dispatch_request(state, context, finish(std::move(writer), 20));
  require(u16(result.output, order, 24) == 33,
          "horizontal output reflection maps to RANDR ReflectY");

  auto& disabled = layout.states.at(glasswyrm::output::OutputId{20});
  disabled.enabled = false;
  disabled.mode_id = {};
  disabled.logical_x = 0;
  disabled.logical_y = 0;
  disabled.logical_width = 0;
  disabled.logical_height = 0;
  disabled.physical_width = 0;
  disabled.physical_height = 0;
  disabled.refresh_millihertz = 0;
  disabled.primary = false;
  disabled.generation = 12;
  layout.states.at(glasswyrm::output::OutputId{10}).generation = 12;
  layout.root_logical_width = 640;
  layout.root_logical_height = 480;
  layout.generation = 12;
  layout.enabled_output_count = 1;
  require(state.randr().configure_output_layout(layout),
          "disable an output without recycling its object IDs");
  result = window_request(state, context, 25);
  require(u16(result.output, order, 16) == 1 &&
              u16(result.output, order, 18) == 2 &&
              u32(result.output, order, 32) == 0x101 &&
              u32(result.output, order, 36) == 0x100 &&
              u32(result.output, order, 40) == 0x103,
          "disabled outputs remain reported while their CRTC is withdrawn");
}

void test_configuration_notifications() {
  ServerState state;
  auto layout = multi_output_layout();
  layout.generation = 9;
  for (auto& [id, output] : layout.states) {
    static_cast<void>(id);
    output.generation = layout.generation;
  }
  require(state.randr().configure_output_layout(layout) &&
              state.randr().select(7, state.screen().root_window, 0x7),
          "select all output-configuration RANDR notifications");
  const auto events = build_output_configuration_events(state);
  require(events && events->size() == 5,
          "one screen and two CRTC/output notifications are staged");
  require(std::holds_alternative<RandRScreenChangeNotifyEvent>(
              events->front().event) &&
              std::get<RandRScreenChangeNotifyEvent>(events->front().event)
                      .config_timestamp == layout.generation,
          "screen notification uses the committed layout generation");
  for (const auto& intent : *events)
    require(intent.delivery == ProtocolEventDelivery::DirectClient &&
                intent.client == 7,
            "RANDR configuration notifications target only selectors");

  ServerState unselected;
  require(unselected.randr().configure_output_layout(layout) &&
              build_output_configuration_events(unselected)->empty(),
          "unselected or rejected configurations enqueue no notifications");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_reporting(order);
    test_multi_output_reporting(order);
  }
  test_configuration_notifications();
  return 0;
}
