#include "gwcomp/compositor.hpp"
#include "gwcomp/output_inventory_publisher.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

class FakePresenter final : public glasswyrm::output::PresentationBackend {
 public:
  std::optional<glasswyrm::output::VrrPresentationCapability>
  vrr_capability(const std::uint64_t output_id) const noexcept override {
    if (output_id != 5)
      return std::nullopt;
    glasswyrm::output::VrrPresentationCapability value;
    value.connected = true;
    value.kms_controllable = true;
    value.simulated = true;
    value.range_available = true;
    value.atomic_required = true;
    value.timing_available = true;
    value.minimum_refresh_millihertz = 40'000;
    value.maximum_refresh_millihertz = 60'000;
    return value;
  }
  glasswyrm::output::PresentResult present(
      const glasswyrm::output::SoftwareFrameView&) override {
    return {};
  }
  int poll_fd() const noexcept override { return -1; }
  short poll_events() const noexcept override { return 0; }
  glasswyrm::output::BackendEvent service(short) override { return {}; }
  glasswyrm::output::BackendStateResult suspend(std::string&) override {
    return glasswyrm::output::BackendStateResult::Complete;
  }
  glasswyrm::output::PresentResult resume(
      const glasswyrm::output::SoftwareFrameView&) override {
    return {};
  }
  glasswyrm::output::BackendStateResult shutdown(
      std::string&) noexcept override {
    return glasswyrm::output::BackendStateResult::Complete;
  }
};

glasswyrm::output::OutputLayout layout() {
  using namespace glasswyrm::output;
  constexpr OutputId id{5};
  constexpr OutputModeId mode{6};
  OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "HEADLESS-1";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = kAllOutputTransformsMask;
  descriptor.minimum_scale = {1, 1};
  descriptor.maximum_scale = {4, 1};
  descriptor.maximum_scale_denominator = kMaximumScaleDenominator;
  descriptor.maximum_physical_width = kMaximumPhysicalExtent;
  descriptor.maximum_physical_height = kMaximumPhysicalExtent;
  descriptor.maximum_physical_pixels = kMaximumPhysicalPixels;
  descriptor.modes.push_back(
      {mode, id, 800, 600, 60'000, 0, "800x600", true, true});
  OutputState state;
  state.output_id = id;
  state.enabled = true;
  state.mode_id = mode;
  state.logical_width = state.physical_width = 800;
  state.logical_height = state.physical_height = 600;
  state.refresh_millihertz = 60'000;
  state.scale = {1, 1};
  state.primary = true;
  state.generation = 1;
  OutputLayout value;
  value.descriptors.emplace(id, std::move(descriptor));
  value.states.emplace(id, state);
  value.primary_output_id = id;
  value.root_logical_width = 800;
  value.root_logical_height = 600;
  value.generation = 1;
  value.enabled_output_count = 1;
  value.output_order = {id};
  return value;
}

void test_bootstrap_inventory() {
  auto presenter = std::make_unique<FakePresenter>();
  gw::compositor::Compositor compositor(std::move(presenter));
  std::string error;
  gw::test::require(compositor.configure_vrr_contract(true, error), error);
  const auto outputs = layout();
  const auto snapshot = compositor.vrr_inventory(outputs, error);
  gw::test::require(
      snapshot && snapshot->capabilities.size() == 1 &&
          snapshot->policies.size() == 1 && snapshot->states.size() == 1 &&
          snapshot->timings.empty() &&
          snapshot->capabilities.front().simulated == 1 &&
          snapshot->policies.front().mode == GWIPC_VRR_POLICY_OFF &&
          snapshot->states.front().decision == GWIPC_VRR_DECISION_DISABLED &&
          snapshot->states.front().state_generation == outputs.generation,
      "negotiated compositor exposes complete deterministic startup VRR state");

  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = 9;
  query.flags = GWIPC_OUTPUT_QUERY_LAYOUT | GWIPC_OUTPUT_QUERY_VRR;
  const glasswyrm::compositor::OutputInventoryVrr vrr{
      snapshot->capabilities, snapshot->policies, snapshot->states,
      snapshot->timings, {}};
  const auto publication =
      glasswyrm::compositor::build_output_inventory_publication(
          query, 11, 12, outputs, {}, &vrr);
  gw::test::require(publication && publication.messages.size() == 7 &&
                        publication.messages[2].type ==
                            GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT &&
                        publication.messages[3].type ==
                            GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT &&
                        publication.messages[4].type ==
                            GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                    "M14 query publishes layout then exact VRR record groups");

  query.flags = GWIPC_OUTPUT_QUERY_LAYOUT;
  const auto historical =
      glasswyrm::compositor::build_output_inventory_publication(
          query, 11, 13, outputs);
  gw::test::require(historical && historical.messages.size() == 4,
                    "historical query remains byte-shaped without VRR records");
}

}  // namespace

int main() { test_bootstrap_inventory(); }
