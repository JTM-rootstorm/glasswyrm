#include "glasswyrmd/output_configuration_coordinator.hpp"
#include "tests/helpers/test_support.hpp"

using namespace glasswyrm;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

output::OutputLayout inventory() {
  constexpr output::OutputId id{5};
  constexpr output::OutputModeId mode{6};
  output::OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "HEADLESS-1";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
  descriptor.minimum_scale = {1, 1};
  descriptor.maximum_scale = {4, 1};
  descriptor.maximum_scale_denominator = output::kMaximumScaleDenominator;
  descriptor.maximum_physical_width = output::kMaximumPhysicalExtent;
  descriptor.maximum_physical_height = output::kMaximumPhysicalExtent;
  descriptor.maximum_physical_pixels = output::kMaximumPhysicalPixels;
  descriptor.modes.push_back({mode, id, 800, 600, 60'000, 0, "800x600",
                              true, true});
  output::OutputState state;
  state.output_id = id;
  state.enabled = true;
  state.mode_id = mode;
  state.logical_width = state.physical_width = 800;
  state.logical_height = state.physical_height = 600;
  state.refresh_millihertz = 60'000;
  state.scale = {1, 1};
  state.primary = true;
  state.generation = 1;
  output::OutputLayout layout;
  layout.descriptors.emplace(id, std::move(descriptor));
  layout.states.emplace(id, state);
  layout.primary_output_id = id;
  layout.root_logical_width = 800;
  layout.root_logical_height = 600;
  layout.generation = 1;
  layout.enabled_output_count = 1;
  layout.output_order = {id};
  return layout;
}

gw::ipc::wire::OutputUpsert output_record() {
  gw::ipc::wire::OutputUpsert value;
  value.output_id = 5;
  value.enabled = true;
  value.logical_width = 800;
  value.logical_height = 600;
  value.physical_pixel_width = 800;
  value.physical_pixel_height = 600;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.transform = gw::ipc::wire::Transform::Normal;
  return value;
}

gwipc_output_vrr_policy_upsert policy(const gwipc_vrr_policy_mode mode) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 5;
  value.mode = mode;
  return value;
}

void begin(OutputConfigurationCoordinator& coordinator, std::uint64_t id,
           gwipc_vrr_policy_mode mode) {
  require(coordinator.begin_snapshot(id, 2) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "begin complete M14 output transaction");
  require(coordinator.stage_output(output_record()) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "stage geometry record");
  require(coordinator.stage_vrr_policy(policy(mode)) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "stage VRR policy record");
  require(coordinator.end_snapshot(id, 2) ==
              OutputConfigurationSnapshotStatus::Accepted,
          "complete exact geometry and policy set");
}

void test_commit_and_rollback() {
  OutputConfigurationCoordinator coordinator(
      inventory(), {{5, GWIPC_VRR_POLICY_OFF}});
  require(coordinator.valid() && coordinator.vrr_profile(),
          "coordinator accepts complete initial VRR policy map");
  begin(coordinator, 10, GWIPC_VRR_POLICY_FOCUSED);
  require(!coordinator.submit({10, 1, 5, 0}) &&
              coordinator.transaction()->old_vrr_policies.at(5) ==
                  GWIPC_VRR_POLICY_OFF &&
              coordinator.transaction()->proposed_vrr_policies.at(5) ==
                  GWIPC_VRR_POLICY_FOCUSED,
          "transaction retains exact old and proposed policy maps");
  require(coordinator.accept_policy() &&
              coordinator.begin_rollback(
                  gw::ipc::wire::OutputConfigurationResult::CompositorRejected),
          "rejected presenter enters rollback");
  const auto rolled_back = coordinator.finish_rollback(true);
  require(rolled_back &&
              coordinator.committed_vrr_policies().at(5) ==
                  GWIPC_VRR_POLICY_OFF,
          "rollback leaves published policy untouched");

  begin(coordinator, 11, GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE);
  require(!coordinator.submit({11, 1, 5, 0}) &&
              coordinator.accept_policy() &&
              coordinator.accept_compositor(),
          "accepted transaction reaches commit boundary");
  const auto committed = coordinator.commit();
  require(committed &&
              coordinator.committed_vrr_policies().at(5) ==
                  GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE &&
              coordinator.committed_layout().generation == 2,
          "geometry generation and VRR policy promote atomically");
}

void test_incomplete_policy_set_rejected() {
  OutputConfigurationCoordinator coordinator(
      inventory(), {{5, GWIPC_VRR_POLICY_OFF}});
  require(coordinator.begin_snapshot(20, 1) ==
              OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.stage_output(output_record()) ==
                  OutputConfigurationSnapshotStatus::Accepted &&
              coordinator.end_snapshot(20, 1) ==
                  OutputConfigurationSnapshotStatus::CountMismatch,
          "M14 snapshot cannot omit its VRR policy map");
  const auto result = coordinator.submit({20, 1, 5, 0});
  require(result && result->result ==
                        gw::ipc::wire::OutputConfigurationResult::InvalidLayout,
          "incomplete M14 snapshot rejects without transaction state");
}

}  // namespace

int main() {
  test_commit_and_rollback();
  test_incomplete_policy_set_rejected();
}
