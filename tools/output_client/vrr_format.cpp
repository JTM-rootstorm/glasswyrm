#include "output_client/output_client.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace glasswyrm::tools::output_client {
namespace {

struct ReasonName {
  std::uint64_t bit;
  std::string_view name;
};

constexpr std::array kReasons{
    ReasonName{GWIPC_VRR_REASON_OUTPUT_DISABLED, "output-disabled"},
    ReasonName{GWIPC_VRR_REASON_OUTPUT_NOT_CONNECTED, "output-not-connected"},
    ReasonName{GWIPC_VRR_REASON_OUTPUT_NOT_DRM, "output-not-drm"},
    ReasonName{GWIPC_VRR_REASON_OUTPUT_NOT_VRR_CAPABLE, "output-not-vrr-capable"},
    ReasonName{GWIPC_VRR_REASON_ATOMIC_KMS_UNAVAILABLE, "atomic-kms-unavailable"},
    ReasonName{GWIPC_VRR_REASON_VRR_PROPERTY_MISSING, "vrr-property-missing"},
    ReasonName{GWIPC_VRR_REASON_VRR_ATOMIC_TEST_FAILED, "vrr-atomic-test-failed"},
    ReasonName{GWIPC_VRR_REASON_SESSION_INACTIVE, "session-inactive"},
    ReasonName{GWIPC_VRR_REASON_VT_SUSPENDED, "vt-suspended"},
    ReasonName{GWIPC_VRR_REASON_OUTPUT_CONFIGURATION_BUSY, "output-configuration-busy"},
    ReasonName{GWIPC_VRR_REASON_POLICY_OFF, "policy-off"},
    ReasonName{GWIPC_VRR_REASON_NO_CANDIDATE, "no-candidate"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_MISSING, "window-missing"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_HIDDEN, "window-hidden"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_UNMANAGED, "window-unmanaged"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_UNFOCUSED, "window-unfocused"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_NOT_FULLSCREEN, "window-not-fullscreen"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_NOT_BORDERLESS_FULLSCREEN, "window-not-borderless-fullscreen"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_SPANS_OUTPUTS, "window-spans-outputs"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_PREFERENCE_DISABLED, "window-preference-disabled"},
    ReasonName{GWIPC_VRR_REASON_WINDOW_DID_NOT_REQUEST, "window-did-not-request"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_MISSING, "surface-missing"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_METADATA_ONLY, "surface-metadata-only"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_NOT_VISIBLE, "surface-not-visible"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_NOT_OPAQUE, "surface-not-opaque"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_ON_WRONG_OUTPUT, "surface-on-wrong-output"},
    ReasonName{GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID, "surface-membership-invalid"},
    ReasonName{GWIPC_VRR_REASON_PRESENTER_REJECTED, "presenter-rejected"},
    ReasonName{GWIPC_VRR_REASON_PROPERTY_READBACK_MISMATCH, "property-readback-mismatch"},
    ReasonName{GWIPC_VRR_REASON_TIMING_UNAVAILABLE, "timing-unavailable"},
    ReasonName{GWIPC_VRR_REASON_HARDWARE_BEHAVIOR_UNCONFIRMED, "hardware-behavior-unconfirmed"},
    ReasonName{GWIPC_VRR_REASON_SIMULATED_HEADLESS, "simulated-headless"},
    ReasonName{GWIPC_VRR_REASON_MANUAL_ALWAYS_ELIGIBLE, "manual-always-eligible"},
};

std::string id(const std::uint64_t value) {
  std::ostringstream result;
  result << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return result.str();
}

std::string json_string(const std::string_view value) {
  std::ostringstream result;
  result << '"';
  for (const unsigned char byte : value) {
    if (byte == '"' || byte == '\\') result << '\\' << byte;
    else if (byte == '\n') result << "\\n";
    else if (byte == '\r') result << "\\r";
    else if (byte == '\t') result << "\\t";
    else if (byte < 0x20U)
      result << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<unsigned>(byte) << std::dec;
    else
      result << byte;
  }
  result << '"';
  return result.str();
}

void json_reasons(std::ostream &output, const std::uint64_t reasons) {
  output << '[';
  bool comma = false;
  for (const auto name : vrr_reason_names(reasons)) {
    if (comma) output << ',';
    output << '"' << name << '"';
    comma = true;
  }
  output << ']';
}

}  // namespace

std::vector<std::string_view> vrr_reason_names(const std::uint64_t reasons) {
  std::vector<std::string_view> result;
  for (const auto &[bit, name] : kReasons)
    if ((reasons & bit) != 0) result.push_back(name);
  return result;
}

void print_vrr(const Snapshot &snapshot,
               const std::optional<std::string_view> selector,
               const bool json, std::ostream &output) {
  bool first = true;
  if (json) output << "{\"vrr\":[";
  for (const auto &[output_id, capability] : snapshot.vrr_capabilities) {
    const auto descriptor = snapshot.descriptors.find(output_id);
    const auto name = descriptor == snapshot.descriptors.end()
                          ? std::string_view{}
                          : std::string_view(descriptor->second.name);
    if (selector && *selector != name && *selector != id(output_id)) continue;
    const auto policy = snapshot.vrr_policies.find(output_id);
    const auto state = snapshot.vrr_outputs.find(output_id);
    const auto timing = snapshot.vrr_timings.find(output_id);
    if (json) {
      if (!first) output << ',';
      output << "{\"id\":\"" << id(output_id) << "\",\"name\":"
             << json_string(name) << ",\"policy\":\""
             << vrr_policy_name(policy == snapshot.vrr_policies.end()
                                    ? GWIPC_VRR_POLICY_OFF
                                    : policy->second)
             << "\",\"property_present\":"
             << (capability.connector_property_present ? "true" : "false")
             << ",\"hardware_capable\":"
             << (capability.hardware_capable ? "true" : "false")
             << ",\"kms_controllable\":"
             << (capability.kms_controllable ? "true" : "false")
             << ",\"simulated\":"
             << (capability.simulated ? "true" : "false")
             << ",\"range_millihertz\":["
             << capability.minimum_refresh_millihertz << ','
             << capability.maximum_refresh_millihertz << ']';
      if (state != snapshot.vrr_outputs.end())
        output << ",\"decision\":\"" << vrr_decision_name(state->second.decision)
               << "\",\"desired_enabled\":"
               << (state->second.desired_enabled ? "true" : "false")
               << ",\"effective_enabled\":"
               << (state->second.effective_enabled ? "true" : "false")
               << ",\"candidate_window\":" << state->second.candidate_window_id
               << ",\"transition_serial\":" << state->second.transition_serial
               << ",\"flip_timestamp_monotonic_ns\":"
               << state->second.last_flip_timestamp_nanoseconds
               << ",\"interval_ns\":" << state->second.last_interval_nanoseconds;
      const auto reasons = state == snapshot.vrr_outputs.end()
                               ? capability.reason_flags
                               : state->second.reason_flags;
      output << ",\"reasons\":";
      json_reasons(output, reasons);
      if (timing != snapshot.vrr_timings.end())
        output << ",\"latest_timing_interval_ns\":"
               << timing->second.interval_nanoseconds;
      output << '}';
    } else {
      output << id(output_id) << ' ' << name << " policy="
             << vrr_policy_name(policy == snapshot.vrr_policies.end()
                                    ? GWIPC_VRR_POLICY_OFF
                                    : policy->second)
             << " hardware=" << capability.hardware_capable
             << " controllable=" << capability.kms_controllable
             << " simulated=" << capability.simulated;
      if (state != snapshot.vrr_outputs.end())
        output << " decision=" << vrr_decision_name(state->second.decision)
               << " desired=" << state->second.desired_enabled
               << " effective=" << state->second.effective_enabled
               << " interval_ns=" << state->second.last_interval_nanoseconds;
      output << '\n';
    }
    first = false;
  }
  if (json) {
    output << "],\"windows\":[";
    first = true;
    for (const auto &[window_id, window] : snapshot.vrr_windows) {
      if (selector) {
        const auto descriptor = snapshot.descriptors.find(window.output_id);
        const auto name = descriptor == snapshot.descriptors.end()
                              ? std::string_view{}
                              : std::string_view(descriptor->second.name);
        if (*selector != name && *selector != id(window.output_id)) continue;
      }
      if (!first) output << ',';
      output << "{\"window\":" << window_id << ",\"surface\":\""
             << id(window.surface_id) << "\",\"output\":\""
             << id(window.output_id) << "\",\"preference\":\""
             << vrr_preference_name(window.preference)
             << "\",\"policy_eligible\":"
             << (window.policy_eligible ? "true" : "false")
             << ",\"selected\":"
             << (window.policy_selected ? "true" : "false")
             << ",\"focused\":" << (window.focused ? "true" : "false")
             << ",\"fullscreen\":"
             << (window.fullscreen ? "true" : "false")
             << ",\"borderless_fullscreen\":"
             << (window.borderless_fullscreen ? "true" : "false")
             << ",\"exclusive_output_membership\":"
             << (window.exclusive_output_membership ? "true" : "false")
             << ",\"policy_generation\":" << window.policy_generation
             << ",\"reasons\":";
      json_reasons(output, window.reason_flags);
      output << '}';
      first = false;
    }
    output << "]}\n";
  } else {
    for (const auto &[window_id, window] : snapshot.vrr_windows) {
      if (selector) {
        const auto descriptor = snapshot.descriptors.find(window.output_id);
        const auto name = descriptor == snapshot.descriptors.end()
                              ? std::string_view{}
                              : std::string_view(descriptor->second.name);
        if (*selector != name && *selector != id(window.output_id)) continue;
      }
      output << "window=" << window_id << " output=" << id(window.output_id)
             << " preference=" << vrr_preference_name(window.preference)
             << " eligible=" << window.policy_eligible
             << " selected=" << window.policy_selected
             << " focused=" << window.focused
             << " fullscreen=" << window.fullscreen
             << " borderless=" << window.borderless_fullscreen
             << " exclusive=" << window.exclusive_output_membership << '\n';
    }
  }
}

}  // namespace glasswyrm::tools::output_client
