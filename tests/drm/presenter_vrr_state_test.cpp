#include "backends/drm/drm_vrr_report.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/kms_vrr_state.hpp"
#include "backends/drm/presenter_vrr.hpp"
#include "tests/helpers/fake_kms.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace glasswyrm;
using namespace glasswyrm::drm;

SavedKmsState saved_state(FakeKmsApi &api) {
  api.connector_crtcs[10] = 40;
  api.crtcs[40] = {40, 60, 0, 0, true, {}};
  api.planes[50] = {50, 60, 40};
  api.properties[{KmsObjectType::Connector, 10}] =
      gw::test::kms_properties({"CRTC_ID"}, 10);
  auto crtc = gw::test::kms_properties({"MODE_ID", "ACTIVE"}, 20);
  crtc.push_back({22, "VRR_ENABLED", 1, 1, PropertyValueRange{0, 1}});
  api.properties[{KmsObjectType::Crtc, 40}] = std::move(crtc);
  api.properties[{KmsObjectType::Plane, 50}] = gw::test::kms_properties(
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
  enabled.nominal_mode_interval_nanoseconds = 16'666'667;
  const auto transition = state.plan(enabled);
  gw::test::require(transition.accepted && transition.include_property,
                    "eligible transition requests one property-bearing flip");
  state.complete_initial(false, true);
  state.complete_flip(true, true, true, 1, 10, 1'000'000'000, true);
  state.complete_flip(true, true, true, 1, 11, 1'016'666'666, true);
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
  mistimed.nominal_mode_interval_nanoseconds = 16'666'666;
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

void timing_period_lifecycle() {
  KmsVrrState kms;
  kms.status = KmsVrrStatus::Controllable;
  kms.connector_property_present = true;
  kms.hardware_capable = true;
  kms.atomic_available = true;
  kms.crtc_property_present = true;
  kms.test_off_passed = kms.test_on_passed = kms.controllable = true;

  PresenterVrrState state;
  state.initialize(7, kms, true, 60'000);
  state.complete_initial(false, true);
  state.complete_flip(false, false, true, 10,
                      std::numeric_limits<std::uint32_t>::max(),
                      1'000'000'000, true);
  state.complete_flip(false, false, true, 10, 0, 1'000'000'100, true);
  gw::test::require(
      state.feedback().interval_nanoseconds == 100 &&
          state.timing_summary().count == 1,
      "sequence wrap remains inside one coherent timing period");

  state.complete_flip(false, false, true, 11, 1, 1'000'000'200, true);
  gw::test::require(
      state.feedback().timestamp_available &&
          state.feedback().interval_nanoseconds == 0 &&
          state.timing_summary().count == 0,
      "transition serial change resets an unchanged effective-state period");
  state.complete_flip(false, false, true, 11, 2, 1'000'000'300, true);
  gw::test::require(state.feedback().interval_nanoseconds == 100 &&
                        state.timing_summary().count == 1,
                    "second same-period sample produces interval evidence");

  state.complete_flip(true, true, true, 12, 3, 1'000'000'400, true);
  gw::test::require(
      state.feedback().effective_enabled &&
          state.feedback().interval_nanoseconds == 0 &&
          state.timing_summary().count == 0,
      "effective-state change starts a fresh enabled timing period");

  state.complete_flip(true, true, true, 12, 4, 0, false);
  gw::test::require(
      !state.feedback().timestamp_available &&
          state.feedback().kernel_timestamp_nanoseconds == 0 &&
          state.feedback().interval_nanoseconds == 0 &&
          state.timestamp_unavailable_count() == 1 &&
          state.timing_summary().count == 0,
      "missing timestamp clears the period without fabricating an interval");
  state.complete_flip(true, true, true, 12, 5, 1'000'001'000, true);
  gw::test::require(
      state.feedback().timestamp_available &&
          state.feedback().interval_nanoseconds == 0,
      "first timestamp after evidence loss is baseline only");
  state.complete_flip(true, true, true, 12, 6, 1'000'001'100, true);
  gw::test::require(state.feedback().interval_nanoseconds == 100 &&
                        state.timing_summary().count == 1,
                    "second recovered timestamp begins interval evidence");

  state.complete_flip(true, true, true, 12, 7, 1'000'001'050, true);
  gw::test::require(
      !state.feedback().timestamp_available &&
          state.feedback().interval_nanoseconds == 0 &&
          state.timestamp_unavailable_count() == 2 &&
          state.timing_summary().count == 0,
      "timestamp regression degrades evidence and resets current statistics");
  state.complete_flip(true, true, true, 12, 8, 1'000'001'150, true);
  gw::test::require(
      state.feedback().timestamp_available &&
          state.feedback().interval_nanoseconds == 100 &&
          state.timing_summary().count == 1,
      "a regression timestamp becomes only the next period baseline");

  state.mark_suspended_off();
  state.mark_acquired_off();
  state.mark_session_active();
  state.complete_flip(true, true, true, 13, 9, 2'000'000'000, true);
  gw::test::require(
      state.feedback().timestamp_available &&
          state.feedback().interval_nanoseconds == 0 &&
          state.timing_summary().count == 0,
      "first post-acquire timestamp cannot span VT downtime");
  state.complete_flip(true, true, true, 13, 10, 2'000'000'100, true);
  gw::test::require(
      state.feedback().interval_nanoseconds == 100 &&
          state.timing_summary().count == 1 &&
          state.enabled_period_count() == 2 &&
          state.disabled_period_count() == 4,
      "current statistics and lifetime state-period counters remain separate");

  PresenterVrrState restarted;
  restarted.initialize(7, kms, true, 60'000);
  gw::test::require(!restarted.feedback().timestamp_available &&
                        restarted.feedback().interval_nanoseconds == 0 &&
                        restarted.timing_summary().count == 0,
                    "presenter restart begins without an inherited baseline");
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

  const DrmVrrTimingReport timing{
      2, 2, 11, 1'014'285'714, 14'285'714, 6'944'444, true};
  const auto encoded_timing =
      serialize_drm_vrr_report_record(DrmVrrReportRecord{timing});
  gw::test::require(
      valid_drm_vrr_report_record(DrmVrrReportRecord{timing}) &&
          encoded_timing.find(
              "\"nominal_mode_interval_nanoseconds\":6944444") !=
              std::string::npos &&
          encoded_timing.find("\"interval_nanoseconds\":14285714") !=
              std::string::npos &&
          encoded_timing.find("target_interval_nanoseconds") ==
              std::string::npos &&
          encoded_timing.find("within_threshold") == std::string::npos,
      "DRM timing reports distinguish a 144 Hz mode from raw 70 Hz cadence");

  const DrmVrrSummaryReport summary{
      3, 14'000'000, 15'000'000, 14'333'333, 14'285'714, 1, 1, 2};
  const auto encoded_summary =
      serialize_drm_vrr_report_record(DrmVrrReportRecord{summary});
  gw::test::require(
      valid_drm_vrr_report_record(DrmVrrReportRecord{summary}) &&
          encoded_summary.find("\"sample_count\":3") != std::string::npos &&
          encoded_summary.find("\"median_nanoseconds\":14285714") !=
              std::string::npos &&
          encoded_summary.find("\"timestamp_unavailable_count\":2") !=
              std::string::npos &&
          encoded_summary.find("pass_count") == std::string::npos &&
          encoded_summary.find("pass_basis_points") == std::string::npos &&
          encoded_summary.find("absolute_error") == std::string::npos,
      "DRM timing summaries contain raw interval statistics without a verdict");
}

} // namespace

int main() {
  probe_and_requests();
  controller_feedback();
  timing_period_lifecycle();
  deterministic_reports();
  return 0;
}
