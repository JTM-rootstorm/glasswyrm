#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>

namespace {

using namespace glasswyrm::server;
using namespace glasswyrm::server::extensions;
namespace x11 = gw::protocol::x11;
using gw::test::require;

constexpr std::uint32_t kWindow = UINT32_C(0x00400001);

x11::FramedRequest request(const x11::ByteOrder order, const std::uint8_t minor,
                           const std::initializer_list<std::uint32_t> fields) {
  x11::ByteWriter writer(order);
  writer.write_u8(kGwVrrMajorOpcode);
  writer.write_u8(minor);
  writer.write_u16(static_cast<std::uint16_t>(1 + fields.size()));
  for (const auto field : fields)
    writer.write_u32(field);
  x11::FramedRequest result;
  result.opcode = kGwVrrMajorOpcode;
  result.data = minor;
  result.bytes = std::move(writer).take();
  result.length_units = result.bytes.size() / 4U;
  return result;
}

std::uint16_t u16(const std::vector<std::uint8_t> &bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint16_t value{};
  require(reader.read_u16(value), "decode GW_VRR u16 fixture field");
  return value;
}

std::uint32_t u32(const std::vector<std::uint8_t> &bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode GW_VRR u32 fixture field");
  return value;
}

std::string fixture(const x11::ByteOrder order) {
  ServerState state;
  VrrWindowStateStore vrr;
  WindowCreateSpec spec;
  spec.xid = kWindow;
  spec.parent = state.screen().root_window;
  spec.width = 640;
  spec.height = 480;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(1, 0x00400000, 0x001fffff, spec) ==
              CreateWindowStatus::Success,
          "create M14 GW_VRR fixture window");
  const DispatchContext context{1, 0x00400000, 0x001fffff, 0x12345, order};

  const auto version =
      dispatch_gw_vrr(state, vrr, context, request(order, 0, {0, 1}));
  require(version.dispatch.output.size() == 32 &&
              u32(version.dispatch.output, order, 8) == 0 &&
              u32(version.dispatch.output, order, 12) == 1,
          "negotiate GW_VRR 0.1 fixture");
  const auto selected = dispatch_gw_vrr(
      state, vrr, context, request(order, 1, {kWindow, kKnownVrrEventMask}));
  require(selected.dispatch.output.empty() &&
              vrr.find_window(kWindow)->event_selections.at(1) ==
                  kKnownVrrEventMask,
          "select every GW_VRR notification class");
  vrr.find_window(kWindow)->preference = WindowVrrPreference::Disable;

  constexpr std::array preferences{
      WindowVrrPreference::Default, WindowVrrPreference::Allow,
      WindowVrrPreference::Prefer, WindowVrrPreference::Disable};
  constexpr std::array names{"default", "allow", "prefer", "disable"};
  std::ostringstream transitions;
  for (std::size_t index = 0; index < preferences.size(); ++index) {
    const auto before = *vrr.find_window(kWindow);
    auto result = dispatch_gw_vrr(
        state, vrr, context,
        request(order, 3,
                {kWindow, static_cast<std::uint32_t>(preferences[index])}));
    require(result.preference_change && result.dispatch.output.size() == 32 &&
                u16(result.dispatch.output, order, 8) ==
                    static_cast<std::uint16_t>(preferences[index]),
            "GW_VRR preference reply accepts exact enum");
    auto after = before;
    after.preference = preferences[index];
    after.primary_output = 0x100;
    after.policy_eligible = index == 1 || index == 2;
    after.selected_candidate = index == 2;
    after.effective_output_enabled = index == 2;
    after.reason_flags = index == 0 ? UINT64_C(1) << 10U : 0;
    after.output_state_generation = 10 + index;
    after.event_selections = before.event_selections;
    const auto notifications =
        gw_vrr_notifications(order, 0x12345 + index, kWindow, before, after,
                             OutputVrrPolicyMode::AppRequested);
    require(notifications.size() == 1 && notifications.front().client == 1 &&
                notifications.front().bytes.size() == 32 &&
                notifications.front().bytes.front() == kGwVrrEventBase &&
                u32(notifications.front().bytes, order, 4) == kWindow &&
                u16(notifications.front().bytes, order, 12) ==
                    static_cast<std::uint16_t>(preferences[index]),
            "GW_VRR preference transition emits byte-order-correct event");
    *vrr.find_window(kWindow) = after;
    if (index != 0)
      transitions << ",";
    transitions << "{\"requested\":\"" << names[index] << "\",\"reply\":"
                << static_cast<std::uint16_t>(preferences[index])
                << ",\"event_detail\":"
                << static_cast<unsigned>(notifications.front().bytes[1]) << "}";
  }

  std::ostringstream output;
  output << "{\n"
         << "  \"schema\": \"glasswyrm.m14-gw-vrr-wire.v1\",\n"
         << "  \"byte_order\": \""
         << (order == x11::ByteOrder::LittleEndian ? "little" : "big")
         << "\",\n"
         << "  \"extension\": {\"major_opcode\": 136, \"event_base\": 70, "
            "\"major\": 0, \"minor\": 1},\n"
         << "  \"selected_event_mask\": 7,\n"
         << "  \"preference_transitions\": [" << transitions.str() << "]\n"
         << "}\n";
  return output.str();
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "open M14 GW_VRR fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main(const int argc, char **argv) {
  require(argc == 2, "usage: m14_gw_vrr_fixture_test FIXTURE_DIR");
  const std::filesystem::path root(argv[1]);
  require(fixture(x11::ByteOrder::LittleEndian) ==
              read_file(root / "gw-vrr-little.json"),
          "little-endian GW_VRR fixture is current");
  require(fixture(x11::ByteOrder::BigEndian) ==
              read_file(root / "gw-vrr-big.json"),
          "big-endian GW_VRR fixture is current");
  return 0;
}
