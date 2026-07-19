#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "output/model/layout.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>

namespace {

using namespace glasswyrm::server;
using namespace glasswyrm::server::extensions;
namespace x11 = gw::protocol::x11;
using gw::test::require;

constexpr std::uint32_t kWindow = 0x400001;
constexpr std::uint32_t kNested = 0x400002;
constexpr std::uint32_t kForeign = 0x600001;
constexpr std::uint32_t kRandrOutput = 0x100;

x11::FramedRequest request(const x11::ByteOrder order,
                           const std::uint8_t minor,
                           const std::initializer_list<std::uint32_t> fields) {
  x11::ByteWriter writer(order);
  writer.write_u8(kGwVrrMajorOpcode);
  writer.write_u8(minor);
  writer.write_u16(static_cast<std::uint16_t>(1 + fields.size()));
  for (const auto value : fields) writer.write_u32(value);
  x11::FramedRequest result;
  result.opcode = kGwVrrMajorOpcode;
  result.data = minor;
  result.bytes = std::move(writer).take();
  result.length_units = result.bytes.size() / 4U;
  return result;
}

std::uint16_t u16(const std::vector<std::uint8_t>& bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span(bytes).subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "decode GW_VRR u16");
  return value;
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span(bytes).subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode GW_VRR u32");
  return value;
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t base, const std::uint32_t xid,
                   const std::uint32_t parent) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = parent;
  spec.width = 320;
  spec.height = 240;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, base, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create GW_VRR test window");
}

void install_output(ServerState& state) {
  using namespace glasswyrm::output;
  constexpr OutputId output_id{0x51};
  constexpr OutputModeId mode_id{0x61};
  OutputDescriptor descriptor;
  descriptor.id = output_id;
  descriptor.name = "VRR-OUT";
  descriptor.connected = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.supported_transform_mask = kAllOutputTransformsMask;
  descriptor.modes.push_back(
      {mode_id, output_id, 800, 600, 60'000, 0, "800x600", true, true});
  OutputState output;
  output.output_id = output_id;
  output.mode_id = mode_id;
  output.enabled = true;
  output.logical_width = 800;
  output.logical_height = 600;
  output.physical_width = 800;
  output.physical_height = 600;
  output.refresh_millihertz = 60'000;
  output.primary = true;
  OutputLayout layout;
  layout.descriptors.emplace(output_id, std::move(descriptor));
  layout.states.emplace(output_id, output);
  layout.output_order = {output_id};
  layout.primary_output_id = output_id;
  layout.root_logical_width = 800;
  layout.root_logical_height = 600;
  layout.enabled_output_count = 1;
  layout.generation = 1;
  require(state.randr().configure_output_layout(layout),
          "install GW_VRR output layout");
}

void require_extension_error(const VrrDispatchResult& result,
                             const std::uint8_t code,
                             const x11::ByteOrder order,
                             const std::uint32_t bad_value) {
  require(result.dispatch.output.size() == 32 &&
              result.dispatch.output[0] == 0 &&
              result.dispatch.output[1] == code &&
              u32(result.dispatch.output, order, 4) == bad_value &&
              result.dispatch.output[10] == kGwVrrMajorOpcode,
          "GW_VRR extension error carries fixed code and bad value");
}

void test_dispatch(const x11::ByteOrder order) {
  ServerState state;
  VrrWindowStateStore vrr;
  create_window(state, 1, 0x400000, kWindow, state.screen().root_window);
  create_window(state, 1, 0x400000, kNested, kWindow);
  create_window(state, 2, 0x600000, kForeign, state.screen().root_window);
  DispatchContext owner{1, 0x400000, 0x1fffff, 0x12345, order};
  DispatchContext other{2, 0x600000, 0x1fffff, 7, order};

  auto result = dispatch_gw_vrr(
      state, vrr, owner, request(order, 0, {0, 99}));
  require(result.dispatch.output.size() == 32 &&
              u32(result.dispatch.output, order, 8) == 0 &&
              u32(result.dispatch.output, order, 12) == 1,
          "QueryVersion negotiates GW_VRR 0.1");

  result = dispatch_gw_vrr(
      state, vrr, owner,
      request(order, 1, {kWindow, kKnownVrrEventMask}));
  require(result.dispatch.output.empty() &&
              vrr.find_window(kWindow)->event_selections.at(1) ==
                  kKnownVrrEventMask,
          "SelectInput stores the complete event mask");
  result = dispatch_gw_vrr(state, vrr, owner,
                           request(order, 1, {kWindow, 8}));
  require(result.dispatch.output.size() == 32 &&
              result.dispatch.output[1] ==
                  static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "SelectInput rejects unknown event bits");

  result = dispatch_gw_vrr(state, vrr, owner,
                           request(order, 2, {kWindow}));
  require(result.dispatch.output.size() == 32 &&
              u16(result.dispatch.output, order, 12) == 0,
          "GetWindowPreference defaults to Default");
  result = dispatch_gw_vrr(
      state, vrr, owner,
      request(order, 3, {kWindow,
                         static_cast<std::uint32_t>(WindowVrrPreference::Prefer)}));
  require(result.preference_change &&
              result.preference_change->window == kWindow &&
              result.preference_change->preference ==
                  WindowVrrPreference::Prefer &&
              vrr.find_window(kWindow)->preference ==
                  WindowVrrPreference::Default,
          "SetWindowPreference defers mutation until lifecycle acceptance");
  require_extension_error(dispatch_gw_vrr(
                              state, vrr, owner,
                              request(order, 3, {kWindow, 4})),
                          kGwVrrBadPreference, order, 4);
  require_extension_error(dispatch_gw_vrr(
                              state, vrr, other,
                              request(order, 3, {kWindow, 3})),
                          kGwVrrBadWindow, order, kWindow);
  require_extension_error(dispatch_gw_vrr(
                              state, vrr, owner,
                              request(order, 3, {kNested, 3})),
                          kGwVrrBadWindow, order, kNested);

  auto& window = vrr.ensure_window(kWindow);
  window.preference = WindowVrrPreference::Prefer;
  window.primary_output = kRandrOutput;
  window.policy_eligible = true;
  window.selected_candidate = true;
  window.effective_output_enabled = true;
  window.reason_flags = UINT64_C(0x0102030405060708);
  window.policy_generation = UINT64_C(0x1112131415161718);
  window.output_state_generation = UINT64_C(0x2122232425262728);
  result = dispatch_gw_vrr(state, vrr, owner,
                           request(order, 4, {kWindow}));
  require(result.dispatch.output.size() == 48 &&
              u32(result.dispatch.output, order, 8) == kWindow &&
              u32(result.dispatch.output, order, 12) == kRandrOutput &&
              u16(result.dispatch.output, order, 16) == 3 &&
              result.dispatch.output[18] == 1 &&
              result.dispatch.output[19] == 1 &&
              result.dispatch.output[20] == 1 &&
              u32(result.dispatch.output, order, 24) == 0x01020304 &&
              u32(result.dispatch.output, order, 28) == 0x05060708,
          "GetWindowState returns preference, policy, effective state, reasons");

  install_output(state);
  auto& output = vrr.ensure_output(kRandrOutput);
  output.policy = OutputVrrPolicyMode::Fullscreen;
  output.connector_property_present = true;
  output.hardware_capable = true;
  output.kms_controllable = true;
  output.effective_enabled = true;
  output.range_available = true;
  output.minimum_refresh_millihertz = 48'000;
  output.maximum_refresh_millihertz = 144'000;
  output.candidate_window = kWindow;
  output.latest_interval_nanoseconds = 13'888'889;
  result = dispatch_gw_vrr(state, vrr, owner,
                           request(order, 5, {kRandrOutput}));
  require(result.dispatch.output.size() == 56 &&
              u32(result.dispatch.output, order, 8) == kRandrOutput &&
              u16(result.dispatch.output, order, 12) == 2 &&
              result.dispatch.output[14] == 1 &&
              result.dispatch.output[15] == 1 &&
              result.dispatch.output[16] == 1 &&
              result.dispatch.output[18] == 1 &&
              result.dispatch.output[19] == 1 &&
              u32(result.dispatch.output, order, 20) == 48'000 &&
              u32(result.dispatch.output, order, 24) == 144'000 &&
              u32(result.dispatch.output, order, 28) == kWindow,
          "GetOutputState reports capability, range, candidate, and timing");
  require_extension_error(dispatch_gw_vrr(
                              state, vrr, owner,
                              request(order, 5, {0x999})),
                          kGwVrrBadWindow, order, 0x999);

  result = dispatch_gw_vrr(state, vrr, owner,
                           request(order, 99, {}));
  require(result.dispatch.output.size() == 32 &&
              result.dispatch.output[1] ==
                  static_cast<std::uint8_t>(x11::CoreErrorCode::BadRequest),
          "unknown GW_VRR minor opcode reports BadRequest");
}

}  // namespace

int main() {
  test_dispatch(x11::ByteOrder::Little);
  test_dispatch(x11::ByteOrder::Big);
  return 0;
}
