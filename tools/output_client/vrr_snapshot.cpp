#include "output_client/vrr_snapshot.hpp"

namespace glasswyrm::tools::output_client {
namespace {

VrrRecordResult invalid(std::string &error, const char *detail) {
  error = detail;
  return VrrRecordResult::Invalid;
}

}  // namespace

VrrRecordResult consume_vrr_snapshot_record(
    const std::uint16_t type, const gwipc_decoded_contract *decoded,
    Snapshot &snapshot, std::string &error) {
  if (type == GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT) {
    const auto *value = gwipc_decoded_output_vrr_capability_upsert(decoded);
    if (!value || snapshot.vrr_capabilities.contains(value->output_id))
      return invalid(error, "VRR snapshot contains duplicate capability state");
    snapshot.vrr_capabilities.emplace(
        value->output_id,
        VrrCapability{value->output_id,
                      value->connector_property_present != 0,
                      value->hardware_capable != 0,
                      value->kms_controllable != 0,
                      value->simulated != 0,
                      value->range_available != 0,
                      value->atomic_required != 0,
                      value->minimum_refresh_millihertz,
                      value->maximum_refresh_millihertz,
                      value->reason_flags});
    return VrrRecordResult::Consumed;
  }
  if (type == GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT) {
    const auto *value = gwipc_decoded_output_vrr_policy_upsert(decoded);
    if (!value ||
        !snapshot.vrr_policies.emplace(value->output_id, value->mode).second)
      return invalid(error, "VRR snapshot contains duplicate output policy");
    return VrrRecordResult::Consumed;
  }
  if (type == GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT) {
    const auto *value = gwipc_decoded_output_vrr_state_upsert(decoded);
    if (!value || snapshot.vrr_outputs.contains(value->output_id))
      return invalid(error, "VRR snapshot contains duplicate effective state");
    snapshot.vrr_outputs.emplace(
        value->output_id,
        VrrOutputState{
            value->output_id, value->requested_mode, value->decision,
            value->desired_enabled != 0, value->effective_enabled != 0,
            value->property_readback_valid != 0, value->session_active != 0,
            value->candidate_window_id, value->candidate_surface_id,
            value->reason_flags, value->state_generation,
            value->transition_serial, value->last_commit_id,
            value->last_presented_generation, value->last_flip_sequence,
            value->last_flip_timestamp_nanoseconds,
            value->last_interval_nanoseconds});
    return VrrRecordResult::Consumed;
  }
  if (type == GWIPC_MESSAGE_SURFACE_VRR_STATE) {
    const auto *value = gwipc_decoded_surface_vrr_state(decoded);
    if (!value || snapshot.vrr_windows.contains(value->window_id))
      return invalid(error, "VRR snapshot contains duplicate window state");
    snapshot.vrr_windows.emplace(
        value->window_id,
        VrrWindowState{
            value->surface_id, value->window_id, value->output_id,
            value->preference, value->policy_selected != 0,
            value->policy_eligible != 0, value->focused != 0,
            value->fullscreen != 0, value->borderless_fullscreen != 0,
            value->exclusive_output_membership != 0, value->reason_flags,
            value->policy_generation});
    return VrrRecordResult::Consumed;
  }
  if (type == GWIPC_MESSAGE_PRESENTATION_TIMING) {
    const auto *value = gwipc_decoded_presentation_timing(decoded);
    if (!value || snapshot.vrr_timings.contains(value->output_id))
      return invalid(error, "VRR snapshot contains duplicate timing state");
    snapshot.vrr_timings.emplace(
        value->output_id,
        VrrTiming{value->output_id, value->commit_id,
                  value->presented_generation, value->flip_sequence,
                  value->flags, value->kernel_timestamp_nanoseconds,
                  value->interval_nanoseconds,
                  value->effective_vrr_enabled != 0,
                  value->timestamp_available != 0});
    return VrrRecordResult::Consumed;
  }
  return VrrRecordResult::NotVrr;
}

bool validate_vrr_snapshot(const Snapshot &snapshot, std::string &error) {
  if (!snapshot.vrr_queried) return true;
  for (const auto &[output_id, unused] : snapshot.outputs) {
    static_cast<void>(unused);
    if (!snapshot.vrr_capabilities.contains(output_id) ||
        !snapshot.vrr_policies.contains(output_id) ||
        !snapshot.vrr_outputs.contains(output_id)) {
      error = "VRR snapshot omits capability, policy, or effective state";
      return false;
    }
  }
  for (const auto &[window_id, state] : snapshot.vrr_windows) {
    const auto window = snapshot.windows.find(window_id);
    if (window == snapshot.windows.end() ||
        window->second.surface_id != state.surface_id) {
      error = "VRR window state does not match the queried scene";
      return false;
    }
  }
  return true;
}

}  // namespace glasswyrm::tools::output_client
