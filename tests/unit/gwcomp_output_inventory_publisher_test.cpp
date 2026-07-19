#include "gwcomp/output_inventory_publisher.hpp"

#include "helpers/test_support.hpp"
#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/output_contract.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace {

using glasswyrm::compositor::build_output_inventory_publication;
using glasswyrm::compositor::OutputInventoryMessage;
using namespace glasswyrm::output;
using namespace gw::ipc::wire;
using gw::test::require;

OutputMode mode(const OutputId output_id, const OutputModeId mode_id,
                const std::uint32_t width, const std::uint32_t height,
                const bool preferred, const bool current,
                const std::uint32_t native_flags = 0) {
  return {mode_id,
          output_id,
          width,
          height,
          60'000,
          native_flags,
          std::to_string(width) + "x" + std::to_string(height),
          preferred,
          current};
}

OutputDescriptor headless_descriptor(const OutputId id,
                                     std::vector<OutputMode> modes) {
  OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "HEADLESS-1";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = kAllOutputTransformsMask;
  descriptor.modes = std::move(modes);
  return descriptor;
}

OutputDescriptor drm_descriptor(const OutputId id, OutputMode value) {
  OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "QXL-1";
  descriptor.kind = glasswyrm::output::OutputKind::Drm;
  descriptor.connected = true;
  descriptor.mode_configurable = false;
  descriptor.primary_eligible = true;
  descriptor.physical_width_mm = 300;
  descriptor.physical_height_mm = 200;
  descriptor.modes.push_back(std::move(value));
  return descriptor;
}

OutputState state(const OutputId output_id, const OutputModeId mode_id,
                  const std::int32_t x, const std::uint32_t physical_width,
                  const std::uint32_t physical_height,
                  const std::uint32_t logical_width,
                  const std::uint32_t logical_height, const RationalScale scale,
                  const bool primary) {
  OutputState result;
  result.output_id = output_id;
  result.enabled = true;
  result.mode_id = mode_id;
  result.logical_x = x;
  result.logical_width = logical_width;
  result.logical_height = logical_height;
  result.physical_width = physical_width;
  result.physical_height = physical_height;
  result.refresh_millihertz = 60'000;
  result.scale = scale;
  result.primary = primary;
  result.generation = 7;
  return result;
}

OutputLayout layout() {
  constexpr OutputId first{UINT64_C(0x8000000000000001)};
  constexpr OutputId second{UINT64_C(0x8000000000000002)};
  constexpr OutputModeId first_mode{UINT64_C(0x4000000000000011)};
  constexpr OutputModeId alternate_mode{UINT64_C(0x4000000000000012)};
  constexpr OutputModeId second_mode{UINT64_C(0x4000000000000021)};
  OutputLayout value;
  value.descriptors.emplace(
      first, headless_descriptor(
                 first, {mode(first, alternate_mode, 1024, 768, false, false),
                         mode(first, first_mode, 800, 600, true, true)}));
  value.descriptors.emplace(
      second,
      drm_descriptor(second,
                     mode(second, second_mode, 640, 480, true, true, 5)));
  value.states.emplace(first, state(first, first_mode, 0, 800, 600, 640, 480,
                                    RationalScale{5, 4}, true));
  value.states.emplace(second, state(second, second_mode, 640, 640, 480, 640,
                                     480, RationalScale{1, 1}, false));
  value.primary_output_id = first;
  value.root_logical_width = 1280;
  value.root_logical_height = 480;
  value.generation = 7;
  value.enabled_output_count = 2;
  value.output_order = {first, second};
  return value;
}

template <typename Value>
Value decode(const OutputInventoryMessage &message,
             CodecStatus (*decoder)(std::span<const std::uint8_t>, Value &)) {
  Value value;
  require(decoder(message.payload, value) == CodecStatus::Ok,
          "publisher record decodes through the authoritative wire codec");
  return value;
}

gwipc_output_state_query query(const std::uint32_t flags) {
  gwipc_output_state_query value{};
  value.struct_size = sizeof(value);
  value.query_id = 41;
  value.flags = flags;
  return value;
}

std::vector<glasswyrm::compositor::OutputInventoryWindow> windows() {
  glasswyrm::compositor::OutputInventoryWindow item;
  item.surface.struct_size = sizeof(item.surface);
  item.surface.surface_id = (UINT64_C(1) << 32U) | 41U;
  item.surface.x11_window_id = 41;
  item.surface.output_id = UINT64_C(0x8000000000000001);
  item.surface.logical_x = 600;
  item.surface.logical_y = 40;
  item.surface.logical_width = 100;
  item.surface.logical_height = 80;
  item.surface.stacking = 2;
  item.surface.visible = 1;
  item.surface.opacity = GWIPC_OPACITY_ONE;
  item.surface.scale_numerator = 2;
  item.surface.scale_denominator = 1;
  item.surface.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                        GWIPC_TRANSFER_FUNCTION_SRGB,
                        GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  item.surface.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  item.policy.struct_size = sizeof(item.policy);
  item.policy.surface_id = item.surface.surface_id;
  item.policy.x11_window_id = 41;
  item.policy.workspace_id = 1;
  item.policy.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  item.policy.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  item.policy.focused = item.policy.managed = 1;
  item.output_ids = {UINT64_C(0x8000000000000001),
                     UINT64_C(0x8000000000000002)};
  item.membership.struct_size = sizeof(item.membership);
  item.membership.surface_id = item.surface.surface_id;
  item.membership.primary_output_id = item.surface.output_id;
  item.membership.preferred_scale_numerator = 5;
  item.membership.preferred_scale_denominator = 4;
  item.membership.client_buffer_scale = 2;
  item.membership.scale_mode = GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  item.membership.layout_generation = 7;
  return {std::move(item)};
}

void test_complete_inventory() {
  const auto value = layout();
  const auto publication = build_output_inventory_publication(
      query(GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
            GWIPC_OUTPUT_QUERY_LAYOUT),
      17, 29, value);
  require(publication && publication.messages.size() == 10,
          "complete query produces begin, seven items, end, and reply");

  const auto &messages = publication.messages;
  const auto begin = decode(
      messages[0],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  SnapshotBegin &)>(gw::ipc::wire::decode));
  require(messages[0].type == GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
              messages[0].flags == 0 && messages[0].reply_to == 0 &&
              begin.snapshot_id == 29 &&
              begin.domain == SnapshotDomain::Outputs &&
              begin.generation == 7 && begin.expected_item_count == 7,
          "snapshot begin fixes Outputs domain, generation, and exact count");

  const auto first_descriptor = decode(
      messages[1], static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                               OutputDescriptorUpsert &)>(
                       gw::ipc::wire::decode));
  const auto second_descriptor = decode(
      messages[2], static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                               OutputDescriptorUpsert &)>(
                       gw::ipc::wire::decode));
  require(messages[1].type == GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT &&
              messages[1].flags == GWIPC_FLAG_SNAPSHOT_ITEM &&
              first_descriptor.name == "HEADLESS-1" &&
              (first_descriptor.capability_flags &
               GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE) != 0 &&
              (first_descriptor.capability_flags &
               GWIPC_OUTPUT_CAP_MODE_FIXED) == 0,
          "configurable headless descriptor maps without ModeFixed");
  require(
      second_descriptor.name == "QXL-1" &&
          (second_descriptor.capability_flags & GWIPC_OUTPUT_CAP_MODE_FIXED) !=
              0 &&
          (second_descriptor.capability_flags &
           GWIPC_OUTPUT_CAP_PHYSICAL_DIMENSIONS_KNOWN) != 0,
      "fixed DRM descriptor maps fixed-mode and physical-size capabilities");

  const auto first_mode = decode(
      messages[3],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  OutputModeUpsert &)>(gw::ipc::wire::decode));
  const auto alternate_mode = decode(
      messages[4],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  OutputModeUpsert &)>(gw::ipc::wire::decode));
  require(first_mode.mode_id == UINT64_C(0x4000000000000011) &&
              first_mode.current &&
              alternate_mode.mode_id == UINT64_C(0x4000000000000012),
          "modes publish deterministically by stable mode ID");
  const auto second_mode = decode(
      messages[5],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  OutputModeUpsert &)>(gw::ipc::wire::decode));
  require(second_mode.flags == 0,
          "native DRM mode flags remain identity-only wire metadata");

  const auto first_state = decode(
      messages[6], static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                               gw::ipc::wire::OutputUpsert &)>(
                       gw::ipc::wire::decode));
  require(
      messages[6].type == GWIPC_MESSAGE_OUTPUT_UPSERT &&
          messages[6].flags == GWIPC_FLAG_SNAPSHOT_ITEM &&
          first_state.output_id == UINT64_C(0x8000000000000001) &&
          first_state.logical_width == 640 &&
          first_state.scale_numerator == 5 &&
          first_state.scale_denominator == 4,
      "current layout record preserves logical and physical scale metadata");

  const auto end = decode(
      messages[8],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  SnapshotEnd &)>(gw::ipc::wire::decode));
  require(messages[8].type == GWIPC_MESSAGE_SNAPSHOT_END &&
              end.snapshot_id == 29 && end.generation == 7 &&
              end.actual_item_count == 7,
          "snapshot end repeats exact identity, generation, and count");

  const auto acknowledged =
      decode(messages[9],
             static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                         OutputConfigurationAcknowledged &)>(
                 gw::ipc::wire::decode));
  require(messages[9].type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED &&
              messages[9].flags == GWIPC_FLAG_REPLY &&
              messages[9].reply_to == 17 && acknowledged.request_id == 41 &&
              acknowledged.applied_generation == 7 &&
              acknowledged.result == OutputConfigurationResult::Accepted &&
              acknowledged.primary_output_id == UINT64_C(0x8000000000000001) &&
              acknowledged.root_logical_width == 1280 &&
              acknowledged.root_logical_height == 480 &&
              acknowledged.enabled_output_count == 2,
          "accepted reply correlates generic sequence and semantic query ID");
}

void test_selective_queries() {
  const auto value = layout();
  auto publication = build_output_inventory_publication(
      query(GWIPC_OUTPUT_QUERY_LAYOUT), 18, 30, value);
  require(publication && publication.messages.size() == 5 &&
              publication.messages[1].type == GWIPC_MESSAGE_OUTPUT_UPSERT &&
              publication.messages[2].type == GWIPC_MESSAGE_OUTPUT_UPSERT,
          "layout-only query excludes descriptor and mode records");
  const auto begin = decode(
      publication.messages.front(),
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  SnapshotBegin &)>(gw::ipc::wire::decode));
  require(begin.expected_item_count == 2,
          "selective query advertises only selected items");

  publication = build_output_inventory_publication(
      query(GWIPC_OUTPUT_QUERY_WINDOWS), 19, 31, value, windows());
  require(publication && publication.messages.size() == 6 &&
              publication.messages[0].type == GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
              publication.messages[1].type == GWIPC_MESSAGE_SURFACE_UPSERT &&
              publication.messages[2].type ==
                  GWIPC_MESSAGE_SURFACE_POLICY_UPSERT &&
              publication.messages[3].type ==
                  GWIPC_MESSAGE_SURFACE_OUTPUT_STATE &&
              publication.messages[4].type == GWIPC_MESSAGE_SNAPSHOT_END &&
              publication.messages[5].type ==
                  GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
          "window-only query returns complete deterministic window records");
  const auto membership = decode(
      publication.messages[3],
      static_cast<CodecStatus (*)(std::span<const std::uint8_t>,
                                  SurfaceOutputState &)>(
          gw::ipc::wire::decode));
  require(membership.output_ids.size() == 2 &&
              membership.primary_output_id ==
                  UINT64_C(0x8000000000000001) &&
              membership.preferred_scale_numerator == 5 &&
              membership.preferred_scale_denominator == 4 &&
              membership.client_buffer_scale == 2,
          "window publication preserves membership and scale state");
}

void test_atomic_rejection() {
  const auto value = layout();
  auto invalid_query = query(GWIPC_OUTPUT_QUERY_LAYOUT);
  invalid_query.reserved[0] = 1;
  auto publication =
      build_output_inventory_publication(invalid_query, 20, 32, value);
  require(!publication && publication.status == GWIPC_STATUS_INVALID_ARGUMENT &&
              publication.messages.empty(),
          "invalid query produces no partial publication");

  auto invalid_layout = value;
  invalid_layout.generation = 0;
  publication = build_output_inventory_publication(
      query(GWIPC_OUTPUT_QUERY_DESCRIPTORS), 21, 33, invalid_layout);
  require(!publication &&
              publication.layout_validation.error ==
                  LayoutValidationError::InvalidGeneration &&
              publication.messages.empty(),
          "invalid core layout produces no snapshot prefix");

  auto inconsistent_dimensions = value;
  inconsistent_dimensions.descriptors.begin()->second.physical_width_mm = 300;
  publication = build_output_inventory_publication(
      query(GWIPC_OUTPUT_QUERY_DESCRIPTORS), 22, 34, inconsistent_dimensions);
  require(!publication &&
              publication.layout_validation.error ==
                  LayoutValidationError::InvalidDescriptor &&
              publication.status == GWIPC_STATUS_INVALID_ARGUMENT &&
              publication.messages.empty(),
          "wire-invalid descriptor metadata fails the whole publication");
}

} // namespace

int main() {
  test_complete_inventory();
  test_selective_queries();
  test_atomic_rejection();
  return 0;
}
