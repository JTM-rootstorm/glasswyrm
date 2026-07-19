#include "output_client/output_client.hpp"

#include <charconv>

namespace glasswyrm::tools::output_client {
namespace {

std::optional<std::uint64_t> parse_id(const std::string_view value) {
  std::uint64_t result{};
  auto text = value;
  int base = 10;
  if (text.starts_with("0x")) {
    text.remove_prefix(2);
    base = 16;
  } else if (text.size() == 16) {
    base = 16;
  }
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result, base);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
             ? std::optional(result)
             : std::nullopt;
}

}  // namespace

std::optional<gwipc_vrr_policy_mode>
parse_vrr_policy(const std::string_view value) noexcept {
  if (value == "off") return GWIPC_VRR_POLICY_OFF;
  if (value == "fullscreen") return GWIPC_VRR_POLICY_FULLSCREEN;
  if (value == "focused") return GWIPC_VRR_POLICY_FOCUSED;
  if (value == "app-requested") return GWIPC_VRR_POLICY_APP_REQUESTED;
  if (value == "always-eligible") return GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE;
  return std::nullopt;
}

const char *vrr_policy_name(const gwipc_vrr_policy_mode value) noexcept {
  switch (value) {
    case GWIPC_VRR_POLICY_OFF: return "off";
    case GWIPC_VRR_POLICY_FULLSCREEN: return "fullscreen";
    case GWIPC_VRR_POLICY_FOCUSED: return "focused";
    case GWIPC_VRR_POLICY_APP_REQUESTED: return "app-requested";
    case GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE: return "always-eligible";
  }
  return "unknown";
}

const char *vrr_preference_name(
    const gwipc_vrr_window_preference value) noexcept {
  switch (value) {
    case GWIPC_VRR_PREFERENCE_DEFAULT: return "default";
    case GWIPC_VRR_PREFERENCE_DISABLE: return "disable";
    case GWIPC_VRR_PREFERENCE_ALLOW: return "allow";
    case GWIPC_VRR_PREFERENCE_PREFER: return "prefer";
  }
  return "unknown";
}

const char *vrr_decision_name(const gwipc_vrr_decision value) noexcept {
  switch (value) {
    case GWIPC_VRR_DECISION_DISABLED: return "disabled";
    case GWIPC_VRR_DECISION_ENABLED: return "enabled";
    case GWIPC_VRR_DECISION_UNSUPPORTED: return "unsupported";
    case GWIPC_VRR_DECISION_REJECTED: return "rejected";
  }
  return "unknown";
}

bool apply_vrr_edit(Snapshot &snapshot, const std::string_view selector,
                    const gwipc_vrr_policy_mode mode, std::string &error) {
  auto output = snapshot.outputs.end();
  for (auto iterator = snapshot.outputs.begin();
       iterator != snapshot.outputs.end(); ++iterator) {
    const auto descriptor = snapshot.descriptors.find(iterator->first);
    if (descriptor != snapshot.descriptors.end() &&
        descriptor->second.name == selector) {
      output = iterator;
      break;
    }
  }
  if (output == snapshot.outputs.end()) {
    const auto id = parse_id(selector);
    if (id) output = snapshot.outputs.find(*id);
  }
  if (output == snapshot.outputs.end()) {
    error = "unknown output '" + std::string(selector) + "'";
    return false;
  }
  if (!snapshot.vrr_queried ||
      !snapshot.vrr_capabilities.contains(output->first)) {
    error = "selected output has no negotiated VRR metadata";
    return false;
  }
  const auto &capability = snapshot.vrr_capabilities.at(output->first);
  if (mode != GWIPC_VRR_POLICY_OFF && !capability.kms_controllable) {
    error = "selected output does not provide controllable VRR";
    return false;
  }
  snapshot.vrr_policies.insert_or_assign(output->first, mode);
  return true;
}

}  // namespace glasswyrm::tools::output_client
