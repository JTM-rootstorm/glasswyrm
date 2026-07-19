#include "backends/drm/drm_vrr_report.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/kms_vrr_state.hpp"
#include "backends/drm/presenter_vrr.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <string>
#include <vector>

namespace {
using namespace glasswyrm;
using namespace glasswyrm::drm;

std::vector<ObjectProperty> properties(
    const std::initializer_list<const char *> names, std::uint32_t id) {
  std::vector<ObjectProperty> result;
  for (const auto *name : names)
    result.push_back({id++, name, 0, 64});
  return result;
}

SavedKmsState saved_state(FakeKmsApi &api) {
  api.connector_crtcs[10] = 40;
  api.crtcs[40] = {40, 60, 0, 0, true, {}};
  api.planes[50] = {50, 60, 40};
  api.properties[{KmsObjectType::Connector, 10}] =
      properties({"CRTC_ID"}, 10);
  auto crtc = properties({"MODE_ID", "ACTIVE"}, 20);
  crtc.push_back({22, "VRR_ENABLED", 1, 1, PropertyValueRange{0, 1}});
  api.properties[{KmsObjectType::Crtc, 40}] = std::move(crtc);
  api.properties[{KmsObjectType::Plane, 50}] = properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
      30);
  SavedKmsState saved;
  std::string error;
  const std::array connector_ids{10U};
  gw::test::require(capture_saved_state(api, 3, {10, 40, 50}, connector_ids,
                                        true, saved, error),
                    error);
  return saved;
}

void probe_and_requests() {
  FakeKmsApi api;
  const auto saved = saved_state(api);
  Connector connector;
  connector.vrr_property_present = true;
  connector.vrr_capable = true;
  const std::array selected{AtomicPropertyValue{50, 30, 70}};
  const auto state = probe_kms_vrr_state(api, 3, connector, {10, 40, 50},
                                         saved, selected);
  gw::test::require(
      state.status == KmsVrrStatus::Controllable && state.controllable &&
          state.original_enabled && state.test_off_passed &&
          state.test_on_passed && api.atomic_commits.size() == 2 &&
          api.atomic_commits[0].properties.size() == 2 &&
          api.atomic_commits[0].properties.back().object_id == 40 &&
          api.atomic_commits[0].properties.back().property_id == 22 &&
          api.atomic_commits[0].properties.back().value == 0 &&
          api.atomic_commits[1].properties.back().value == 1,
      "VRR probe preserves selected state and tests exact off/on values");

  auto flip = make_vrr_atomic_request(selected, state, true);
  gw::test::require(flip.size() == 2 && flip.front().value == 70 &&
                        flip.back().object_id == 40 &&
                        flip.back().property_id == 22 &&
                        flip.back().value == 1,
                    "VRR property joins the selected framebuffer state");

  api.rejected_test_property = std::pair{22U, UINT64_C(1)};
  const auto rejected = probe_kms_vrr_state(api, 3, connector, {10, 40, 50},
                                            saved, selected);
  gw::test::require(
      rejected.status == KmsVrrStatus::TestOnRejected &&
          rejected.test_off_passed && !rejected.test_on_passed &&
          !rejected.controllable,
      "TEST_ONLY on rejection disables VRR without disabling atomic KMS");
}

void controller_feedback() {
  KmsVrrState kms;
  kms.status = KmsVrrStatus::Controllable;
  kms.connector_property_present = true;
  kms.hardware_capable = true;
  kms.atomic_available = true;
  kms.crtc_property_present = true;
  kms.crtc_id = 40;
  kms.crtc_property_id = 22;
  kms.test_off_passed = kms.test_on_passed = kms.controllable = true;
  PresenterVrrState state;
  state.initialize(7, kms, true, 60'000);

  output::VrrPresentationRequest enabled;
  enabled.valid = true;
  enabled.requested_mode = output::vrr::PolicyMode::Fullscreen;
  enabled.decision = output::vrr::Decision::Enabled;
  enabled.desired_enabled = true;
  enabled.target_interval_nanoseconds = 16'666'667;
  const auto transition = state.plan(enabled);
  gw::test::require(transition.accepted && transition.include_property &&
                        transition.requires_flip,
                    "eligible transition requests one property-bearing flip");
  state.complete_initial(false, true);
  state.complete_flip(true, true, true, 10, 1'000'000'000, true);
  state.complete_flip(true, true, true, 11, 1'016'666'666, true);
  const auto feedback = state.feedback();
  gw::test::require(
      feedback.output_id == 7 && feedback.effective_enabled &&
          feedback.property_readback_valid && feedback.flip_sequence == 11 &&
          feedback.timestamp_available &&
          feedback.interval_nanoseconds == 16'666'666 &&
          !state.plan(enabled).include_property,
      "readback and monotonic flip timing become deterministic feedback");

  const auto reaffirmed = state.plan(enabled, true);
  gw::test::require(reaffirmed.include_property,
                    "explicit test injection reaffirms unchanged VRR state");
  auto mistimed = enabled;
  mistimed.target_interval_nanoseconds = 16'666'666;
  gw::test::require(!state.plan(mistimed).accepted,
                    "presenter rejects timing for a different output mode");
  state.mark_suspended_off();
  const auto suspended = state.capability(true, true);
  gw::test::require(
      !suspended.session_active && suspended.suspended &&
          output::vrr::has_reason(suspended.reason_flags,
                                  output::vrr::Reason::SessionInactive) &&
          !state.effective_enabled(),
      "suspend exposes inactive state with VRR forced off");
}

void deterministic_reports() {
  DrmVrrDecisionReport decision;
  decision.commit_id = decision.generation = decision.output_id = 1;
  decision.policy_mode = output::vrr::PolicyMode::Fullscreen;
  decision.desired_enabled = decision.effective_enabled = true;
  decision.reason_flags =
      output::vrr::reason_bit(output::vrr::Reason::ManualAlwaysEligible);
  decision.session_active = true;
  const DrmVrrReportRecord record{decision};
  const auto encoded = serialize_drm_vrr_report_record(record);
  gw::test::require(
      valid_drm_vrr_report_record(record) &&
          encoded.find("\"record\":\"vrr-decision\"") !=
              std::string::npos &&
          encoded.find("\"reasons\":[\"ManualAlwaysEligible\"]") !=
              std::string::npos &&
          encoded.find("timestamp") == std::string::npos,
      "VRR report is stable JSON without wall-clock timestamps");
}

} // namespace

int main() {
  probe_and_requests();
  controller_feedback();
  deterministic_reports();
  return 0;
}
