#include "gwcomp/vrr_response_batch.hpp"
#include "gwcomp/vrr_runtime.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

class FakePresenter final : public glasswyrm::output::PresentationBackend {
public:
  std::optional<glasswyrm::output::VrrPresentationCapability>
  vrr_capability(const std::uint64_t output_id) const noexcept override {
    if (output_id != 1) return std::nullopt;
    glasswyrm::output::VrrPresentationCapability result;
    result.output_enabled = true;
    result.connected = true;
    result.kms_controllable = true;
    result.simulated = true;
    result.session_active = true;
    result.timing_available = true;
    return result;
  }
  glasswyrm::output::PresentResult present(
      const glasswyrm::output::SoftwareFrameView&) override { return {}; }
  int poll_fd() const noexcept override { return -1; }
  short poll_events() const noexcept override { return 0; }
  glasswyrm::output::BackendEvent service(short) override { return {}; }
  glasswyrm::output::BackendStateResult suspend(std::string&) override {
    return glasswyrm::output::BackendStateResult::Complete;
  }
  glasswyrm::output::PresentResult resume(
      const glasswyrm::output::SoftwareFrameView&) override { return {}; }
  glasswyrm::output::BackendStateResult shutdown(
      std::string&) noexcept override {
    return glasswyrm::output::BackendStateResult::Complete;
  }
};

gw::compositor::Scene scene() {
  gw::compositor::Scene value;
  value.configuration_generation = 3;
  value.primary_output_id = 1;
  gwipc_output_upsert output{};
  output.output_id = 1;
  output.enabled = 1;
  value.outputs.emplace(1, output);
  gwipc_surface_upsert surface{};
  surface.surface_id = 10;
  surface.x11_window_id = 42;
  surface.output_id = 1;
  surface.visible = 1;
  surface.opacity = GWIPC_OPACITY_ONE;
  value.surfaces.emplace(10, surface);
  gwipc_surface_policy_upsert surface_policy{};
  surface_policy.surface_id = 10;
  surface_policy.x11_window_id = 42;
  value.surface_policies.emplace(10, surface_policy);
  value.surface_outputs.emplace(
      10, gw::compositor::SurfaceOutputMembership{
              1, {1}, 1, 1, 1, GWIPC_SURFACE_SCALE_LEGACY, 3, 0});
  gwipc_output_vrr_policy_upsert policy{};
  policy.output_id = 1;
  policy.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  value.vrr.output_policies.emplace(1, policy);
  gwipc_surface_vrr_state state{};
  state.surface_id = 10;
  state.window_id = 42;
  state.output_id = 1;
  state.preference = GWIPC_VRR_PREFERENCE_ALLOW;
  state.policy_selected = 1;
  state.policy_eligible = 1;
  state.focused = 1;
  state.fullscreen = 1;
  state.exclusive_output_membership = 1;
  state.policy_generation = 7;
  value.vrr.surfaces.emplace(10, state);
  value.vrr.policy_generation = 7;
  return value;
}

} // namespace

int main() {
  using gw::test::require;
  FakePresenter presenter;
  gw::compositor::CommittedVrrState committed;
  std::string error;
  auto prepared = gw::compositor::VrrRuntime::prepare(
      scene(), presenter, committed, error);
  require(prepared && prepared->requests.at(1).desired_enabled &&
              prepared->requests.at(1).decision ==
                  glasswyrm::output::vrr::Decision::Enabled,
          "pure decision is attached before presentation");

  auto disabled = scene();
  auto second_output = disabled.outputs.at(1);
  second_output.output_id = 2;
  second_output.enabled = 0;
  disabled.outputs.emplace(2, second_output);
  auto second_policy = disabled.vrr.output_policies.at(1);
  second_policy.output_id = 2;
  disabled.vrr.output_policies.emplace(2, second_policy);
  auto enabled_only = gw::compositor::VrrRuntime::prepare(
      disabled, presenter, committed, error);
  require(enabled_only && enabled_only->requests.size() == 1 &&
              enabled_only->requests.contains(1),
          "VRR response metadata covers enabled frame-set outputs only");

  gwipc_frame_commit commit{};
  commit.commit_id = 11;
  commit.producer_generation = 12;
  gw::compositor::VrrResponseBatch::ReleaseMap releases{
      {77, GWIPC_BUFFER_RELEASE_REPLACED}};
  auto batch = gw::compositor::VrrResponseBatch::preflight(
      *prepared, commit, GWIPC_FRAME_ACCEPTED, releases, error);
  require(batch && batch->reserved_messages() == 4,
          "response preflight reserves state timing ack and release");

  glasswyrm::output::VrrPresentationFeedback feedback;
  feedback.output_id = 1;
  feedback.effective_enabled = true;
  feedback.session_active = true;
  feedback.flip_sequence = 9;
  feedback.kernel_timestamp_nanoseconds = 1000000000;
  feedback.interval_nanoseconds = 16666667;
  feedback.timestamp_available = true;
  auto completed = gw::compositor::VrrRuntime::complete(
      *prepared, {{1, feedback}}, 11, 12, error);
  require(completed && batch->finalize(*completed, error),
          "actual presenter feedback completes preflighted response");
  const auto& messages = batch->messages();
  require(messages.size() == 4 &&
              messages[0].type == GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT &&
              messages[1].type == GWIPC_MESSAGE_PRESENTATION_TIMING &&
              messages[2].type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED &&
              messages[3].type == GWIPC_MESSAGE_BUFFER_RELEASE,
          "final response order is state timing acknowledgement releases");
  require(completed->states.at(1).last_commit_id == 11 &&
              completed->states.at(1).last_presented_generation == 12 &&
              completed->timings.at(1).commit_id == 11 &&
              completed->timings.at(1).presented_generation == 12,
          "state and timing correlate to the presented frame");

  auto two_outputs = *prepared;
  two_outputs.requests.emplace(2, two_outputs.requests.at(1));
  two_outputs.capabilities.emplace(2, two_outputs.capabilities.at(1));
  auto two_completed = *completed;
  auto second_state = two_completed.states.at(1);
  second_state.output_id = 2;
  two_completed.states.emplace(2, second_state);
  auto second_timing = two_completed.timings.at(1);
  second_timing.output_id = 2;
  two_completed.timings.emplace(2, second_timing);
  auto ordered = gw::compositor::VrrResponseBatch::preflight(
      two_outputs, commit, GWIPC_FRAME_ACCEPTED, {}, error);
  require(ordered && ordered->finalize(two_completed, error) &&
              ordered->messages().size() == 5 &&
              ordered->messages()[0].type ==
                  GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT &&
              ordered->messages()[1].type ==
                  GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT &&
              ordered->messages()[2].type == GWIPC_MESSAGE_PRESENTATION_TIMING &&
              ordered->messages()[3].type == GWIPC_MESSAGE_PRESENTATION_TIMING &&
              ordered->messages()[4].type ==
                  GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,
          "multi-output responses group every state before every timing");
}
