#include "glasswyrmd/output_configuration_coordinator.hpp"

#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <vector>

namespace {

using namespace glasswyrm;
using namespace glasswyrm::server;
using gw::ipc::wire::OutputConfigurationCommit;
using gw::ipc::wire::OutputConfigurationResult;
using gw::ipc::wire::OutputUpsert;
using gw::test::require;

constexpr output::OutputId kLeft{11};
constexpr output::OutputId kRight{12};

output::OutputDescriptor descriptor(const output::OutputId id,
                                    const output::OutputModeId mode_id,
                                    const char *name,
                                    const std::uint32_t width,
                                    const std::uint32_t height,
                                    const bool arbitrary) {
  output::OutputDescriptor value;
  value.id = id;
  value.name = name;
  value.connected = true;
  value.supported_transform_mask =
      arbitrary ? output::kAllOutputTransformsMask
                : output::output_transform_bit(output::OutputTransform::Normal);
  value.minimum_scale = {1, 1};
  value.maximum_scale = {4, 1};
  value.maximum_scale_denominator = output::kMaximumScaleDenominator;
  value.mode_configurable = true;
  value.scale_configurable = arbitrary;
  value.transform_configurable = arbitrary;
  value.primary_eligible = true;
  value.arbitrary_headless_mode = arbitrary;
  value.maximum_physical_width = output::kMaximumPhysicalExtent;
  value.maximum_physical_height = output::kMaximumPhysicalExtent;
  value.maximum_physical_pixels = output::kMaximumPhysicalPixels;
  value.modes.push_back({mode_id, id, width, height, 60'000, 0,
                         std::string(name) + "-mode", true, true});
  return value;
}

output::OutputState state(const output::OutputId id,
                          const output::OutputModeId mode_id,
                          const std::int32_t x, const std::int32_t y,
                          const std::uint32_t width,
                          const std::uint32_t height, const bool primary) {
  output::OutputState value;
  value.output_id = id;
  value.enabled = true;
  value.mode_id = mode_id;
  value.logical_x = x;
  value.logical_y = y;
  value.logical_width = value.physical_width = width;
  value.logical_height = value.physical_height = height;
  value.refresh_millihertz = 60'000;
  value.scale = {1, 1};
  value.primary = primary;
  value.generation = 1;
  return value;
}

output::OutputLayout inventory() {
  output::OutputLayout value;
  value.descriptors.emplace(kLeft,
                            descriptor(kLeft, {21}, "LEFT", 800, 600, true));
  value.descriptors.emplace(
      kRight, descriptor(kRight, {22}, "RIGHT", 600, 450, false));
  value.states.emplace(kLeft, state(kLeft, {21}, 0, 0, 800, 600, true));
  value.states.emplace(kRight,
                       state(kRight, {22}, 800, 0, 600, 450, false));
  value.primary_output_id = kLeft;
  value.root_logical_width = 1400;
  value.root_logical_height = 600;
  value.generation = 1;
  value.enabled_output_count = 2;
  value.output_order = {kLeft, kRight};
  require(static_cast<bool>(output::validate_layout(value)),
          "test inventory is valid");
  return value;
}

output::OutputLayout drm_inventory() {
  constexpr output::OutputId output_id{31};
  constexpr output::OutputModeId mode_id{41};
  output::OutputLayout value;
  auto output_descriptor =
      descriptor(output_id, mode_id, "Virtual-1", 1024, 768, true);
  output_descriptor.kind = output::OutputKind::Drm;
  output_descriptor.mode_configurable = false;
  output_descriptor.arbitrary_headless_mode = false;
  value.descriptors.emplace(output_id, std::move(output_descriptor));
  value.states.emplace(output_id,
                       state(output_id, mode_id, 0, 0, 1024, 768, true));
  value.primary_output_id = output_id;
  value.root_logical_width = 1024;
  value.root_logical_height = 768;
  value.generation = 1;
  value.enabled_output_count = 1;
  value.output_order = {output_id};
  require(static_cast<bool>(output::validate_layout(value)),
          "test DRM inventory is valid");
  return value;
}

OutputUpsert upsert(const output::OutputState &state) {
  OutputUpsert value;
  value.output_id = state.output_id.value;
  value.enabled = state.enabled;
  value.logical_x = state.logical_x;
  value.logical_y = state.logical_y;
  value.logical_width = state.logical_width;
  value.logical_height = state.logical_height;
  value.physical_pixel_width = state.physical_width;
  value.physical_pixel_height = state.physical_height;
  value.refresh_millihertz = state.refresh_millihertz;
  value.scale_numerator = state.scale.numerator;
  value.scale_denominator = state.scale.denominator;
  value.transform = static_cast<gw::ipc::wire::Transform>(state.transform);
  return value;
}

std::vector<OutputUpsert> records(const output::OutputLayout &layout) {
  std::vector<OutputUpsert> result;
  for (const auto id : layout.output_order)
    result.push_back(upsert(layout.states.at(id)));
  return result;
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
prepare(OutputConfigurationCoordinator &coordinator, const std::uint64_t id,
        const std::uint64_t base, const std::uint64_t primary,
        const std::vector<OutputUpsert> &values) {
  require(coordinator.begin_snapshot(id, values.size()) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "begin complete output snapshot");
  for (const auto &value : values)
    require(coordinator.stage_output(value) ==
                OutputConfigurationSnapshotStatus::Accepted,
            "stage output record");
  require(coordinator.end_snapshot(id, values.size()) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "end complete output snapshot");
  return coordinator.submit({id, base, primary, 0});
}

void require_rejection(OutputConfigurationCoordinator &coordinator,
                       const std::uint64_t id,
                       const std::vector<OutputUpsert> &values,
                       const std::uint64_t primary,
                       const OutputConfigurationResult expected) {
  const auto ack = prepare(coordinator, id,
                           coordinator.committed_layout().generation, primary,
                           values);
  require(ack && ack->request_id == id && ack->result == expected &&
              ack->applied_generation ==
                  coordinator.committed_layout().generation,
          "invalid configuration returns deterministic rejection metadata");
}

void test_transaction_and_rollback() {
  OutputConfigurationCoordinator coordinator(inventory());
  require(coordinator.valid() &&
              coordinator.stage() == OutputConfigurationStage::Idle,
          "coordinator accepts immutable validated inventory");
  auto proposed = records(coordinator.committed_layout());
  proposed[1].logical_x = 0;
  proposed[1].logical_y = 600;
  auto ack = prepare(coordinator, 1, 1, kRight.value, proposed);
  require(!ack && coordinator.stage() ==
                      OutputConfigurationStage::PolicyPending,
          "valid complete snapshot begins one active transaction");
  const auto *transaction = coordinator.transaction();
  require(transaction && transaction->configuration_id == 1 &&
              transaction->old_layout.generation == 1 &&
              transaction->old_layout.root_logical_width == 1400 &&
              transaction->proposed_layout.generation == 2 &&
              transaction->proposed_layout.primary_output_id == kRight &&
              transaction->proposed_layout.root_logical_width == 800 &&
              transaction->proposed_layout.root_logical_height == 1050,
          "transaction retains rollback-ready old and proposed layouts");
  require(coordinator.begin_snapshot(99, 2) ==
              OutputConfigurationSnapshotStatus::Busy,
          "second output snapshot is rejected while commit is active");
  ack = coordinator.submit({99, 1, kLeft.value, 0});
  require(ack && ack->result == OutputConfigurationResult::Busy &&
              ack->request_id == 99 && ack->applied_generation == 1,
          "second tool commit receives a deterministic Busy acknowledgement");
  require(coordinator.accept_policy() &&
              coordinator.begin_rollback(
                  OutputConfigurationResult::CompositorRejected),
          "compositor rejection enters rollback with transaction retained");
  require(coordinator.transaction() &&
              coordinator.transaction()->old_layout.generation == 1,
          "rollback stage continues exposing the exact old layout");
  ack = coordinator.finish_rollback(true);
  require(ack && ack->result ==
                     OutputConfigurationResult::CompositorRejected &&
              ack->applied_generation == 1 &&
              coordinator.committed_layout().generation == 1 &&
              coordinator.stage() == OutputConfigurationStage::Idle,
          "successful rollback leaves committed layout untouched");

  ack = prepare(coordinator, 2, 1, kRight.value, proposed);
  require(!ack, "policy rejection transaction prepares");
  ack = coordinator.reject_policy();
  require(ack && ack->result == OutputConfigurationResult::PolicyRejected &&
              coordinator.committed_layout().generation == 1,
          "policy rejection clears transaction without state mutation");

  ack = prepare(coordinator, 3, 1, kRight.value, proposed);
  require(!ack && coordinator.accept_policy() &&
              coordinator.begin_rollback(
                  OutputConfigurationResult::PresenterRejected),
          "presenter rejection can request rollback");
  ack = coordinator.finish_rollback(false);
  require(ack && ack->result == OutputConfigurationResult::InternalError &&
              ack->applied_generation == 1,
          "rollback failure reports deterministic fatal internal result");

  ack = prepare(coordinator, 31, 1, kRight.value, proposed);
  require(!ack, "internal promotion failure transaction prepares");
  ack = coordinator.fail_internal();
  require(ack && ack->result == OutputConfigurationResult::InternalError &&
              ack->applied_generation == 1 &&
              coordinator.committed_layout().generation == 1 &&
              coordinator.stage() == OutputConfigurationStage::Idle,
          "local promotion failure preserves the exact committed layout");

  ack = prepare(coordinator, 4, 1, kRight.value, proposed);
  require(!ack && coordinator.accept_policy() &&
              coordinator.accept_compositor(),
          "accepted policy and compositor reach commit-ready stage");
  ack = coordinator.commit();
  require(ack && ack->result == OutputConfigurationResult::Accepted &&
              ack->request_id == 4 && ack->applied_generation == 2 &&
              ack->primary_output_id == kRight.value &&
              ack->root_logical_width == 800 &&
              ack->root_logical_height == 1050 &&
              ack->enabled_output_count == 2 &&
              coordinator.committed_layout().generation == 2,
          "final commit atomically exposes the accepted layout metadata");
}

void test_validation_results() {
  OutputConfigurationCoordinator coordinator(inventory());
  const auto current = records(coordinator.committed_layout());

  require(coordinator.begin_snapshot(10, 2) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "begin stale snapshot");
  require(coordinator.stage_output(current[0]) ==
              OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.stage_output(current[1]) ==
                  OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.end_snapshot(10, 2) ==
                  OutputConfigurationSnapshotStatus::Accepted,
          "stage stale but otherwise valid snapshot");
  auto ack = coordinator.submit({10, 0, kLeft.value, 0});
  require(ack && ack->result ==
                     OutputConfigurationResult::StaleGeneration &&
              ack->applied_generation == 1,
          "base generation must match exactly");

  require(coordinator.begin_snapshot(11, 2) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "begin unknown-output snapshot");
  auto unknown = current[1];
  unknown.output_id = 99;
  require(coordinator.stage_output(current[0]) ==
              OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.stage_output(unknown) ==
                  OutputConfigurationSnapshotStatus::InvalidRecord,
          "unknown output is rejected while staging");
  require(coordinator.end_snapshot(11, 2) ==
              OutputConfigurationSnapshotStatus::CountMismatch,
          "unknown record also proves snapshot incompleteness");
  ack = coordinator.submit({11, 1, kLeft.value, 0});
  require(ack && ack->result == OutputConfigurationResult::UnknownOutput,
          "unknown output has its specific acknowledgement result");

  auto invalid = current;
  invalid[1].physical_pixel_width = 601;
  invalid[1].logical_width = 601;
  require_rejection(coordinator, 12, invalid, kLeft.value,
                    OutputConfigurationResult::UnsupportedMode);
  invalid = current;
  invalid[1].scale_numerator = 2;
  invalid[1].logical_width = 300;
  invalid[1].logical_height = 225;
  require_rejection(coordinator, 13, invalid, kLeft.value,
                    OutputConfigurationResult::UnsupportedScale);
  invalid = current;
  invalid[1].transform = gw::ipc::wire::Transform::Rotate90;
  require_rejection(coordinator, 14, invalid, kLeft.value,
                    OutputConfigurationResult::UnsupportedTransform);
  invalid = current;
  invalid[1].logical_x = 700;
  require_rejection(coordinator, 15, invalid, kLeft.value,
                    OutputConfigurationResult::InvalidLayout);
  require_rejection(coordinator, 16, current, 99,
                    OutputConfigurationResult::UnknownOutput);

  require(coordinator.begin_snapshot(17, 2) ==
              OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.stage_output(current[0]) ==
                  OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.end_snapshot(17, 1) ==
                  OutputConfigurationSnapshotStatus::CountMismatch,
          "missing output is a complete-snapshot count failure");
  ack = coordinator.submit({17, 1, kLeft.value, 0});
  require(ack && ack->result == OutputConfigurationResult::InvalidLayout,
          "incomplete snapshot receives InvalidLayout");
}

void test_arbitrary_headless_mode() {
  OutputConfigurationCoordinator coordinator(inventory());
  auto changed = records(coordinator.committed_layout());
  changed[0].physical_pixel_width = 1024;
  changed[0].physical_pixel_height = 768;
  changed[0].scale_numerator = 2;
  changed[0].logical_width = 512;
  changed[0].logical_height = 384;
  changed[1].logical_x = 512;
  auto ack = prepare(coordinator, 20, 1, kLeft.value, changed);
  require(!ack && coordinator.transaction() &&
              coordinator.transaction()
                      ->proposed_layout.descriptors.at(kLeft)
                      .modes.size() == 2 &&
              coordinator.transaction()->proposed_layout.states.at(kLeft)
                      .mode_id != output::OutputModeId{21},
          "arbitrary headless capability derives a bounded stable mode");
  require(coordinator.accept_policy() && coordinator.accept_compositor(),
          "arbitrary mode transaction reaches commit stage");
  ack = coordinator.commit();
  require(ack && ack->result == OutputConfigurationResult::Accepted &&
              coordinator.inventory().descriptors.at(kLeft).modes.size() == 1,
          "committing arbitrary mode does not mutate immutable inventory");

  auto broken = inventory();
  broken.generation = 0;
  OutputConfigurationCoordinator invalid(std::move(broken));
  require(!invalid.valid(), "coordinator rejects invalid initial inventory");
  ack = invalid.submit({1, 1, kLeft.value, 0});
  require(ack && ack->result == OutputConfigurationResult::InternalError,
          "invalid coordinator reports deterministic internal failure");
}

void test_single_output_drm_configuration() {
  constexpr output::OutputId output_id{31};
  OutputConfigurationCoordinator coordinator(drm_inventory());
  const auto current = records(coordinator.committed_layout());

  auto changed = current;
  changed[0].logical_x = 1;
  require_rejection(coordinator, 40, changed, output_id.value,
                    OutputConfigurationResult::InvalidLayout);

  changed = current;
  changed[0].physical_pixel_width = 800;
  changed[0].logical_width = 800;
  require_rejection(coordinator, 41, changed, output_id.value,
                    OutputConfigurationResult::UnsupportedMode);

  changed = current;
  changed[0].scale_numerator = 4;
  changed[0].scale_denominator = 3;
  changed[0].logical_width = 768;
  changed[0].logical_height = 576;
  changed[0].transform = gw::ipc::wire::Transform::Rotate180;
  auto ack = prepare(coordinator, 42, 1, output_id.value, changed);
  const auto *transaction = coordinator.transaction();
  require(!ack && transaction &&
              transaction->proposed_layout.root_logical_width == 768 &&
              transaction->proposed_layout.root_logical_height == 576 &&
              transaction->proposed_layout.states.at(output_id).scale ==
                  output::RationalScale{4, 3} &&
              transaction->proposed_layout.states.at(output_id).transform ==
                  output::OutputTransform::Rotate180 &&
              transaction->proposed_layout.states.at(output_id)
                      .physical_width == 1024 &&
              transaction->proposed_layout.states.at(output_id)
                      .physical_height == 768,
          "single-output DRM accepts compositor scale and transform while "
          "retaining its fixed native mode and origin");
  require(coordinator.accept_policy() && coordinator.accept_compositor(),
          "accepted DRM configuration reaches commit stage");
  ack = coordinator.commit();
  require(ack && ack->result == OutputConfigurationResult::Accepted &&
              ack->root_logical_width == 768 &&
              ack->root_logical_height == 576,
          "accepted DRM scale and transform commit atomically");
}

} // namespace

int main() {
  test_transaction_and_rollback();
  test_validation_results();
  test_arbitrary_headless_mode();
  test_single_output_drm_configuration();
  return 0;
}
