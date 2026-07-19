#include "output/vrr/decision.hpp"

#include "helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

using namespace glasswyrm::output::vrr;
using gw::test::require;

DecisionInput eligible(const PolicyMode mode) {
  DecisionInput input;
  input.mode = mode;
  input.output = {true, true, false, false, false, true, false, true, true};
  input.display = {true, false, false};
  input.presenter = {true, true, true, true};
  input.candidate = {true,
                     41,
                     51,
                     true,
                     true,
                     true,
                     true,
                     true,
                     false,
                     true,
                     WindowPreference::Default,
                     true,
                     false,
                     true,
                     true,
                     true,
                     true};
  return input;
}

std::string decision_name(const Decision value) {
  switch (value) {
  case Decision::Unsupported:
    return "unsupported";
  case Decision::Disabled:
    return "disabled";
  case Decision::Enabled:
    return "enabled";
  case Decision::Rejected:
    return "rejected";
  }
  return {};
}

std::string reason_array(const ReasonMask mask) {
  std::ostringstream output;
  output << "[";
  bool first = true;
  for (std::size_t index = 0; index < kReasonCount; ++index) {
    const auto reason = static_cast<Reason>(index);
    if (!has_reason(mask, reason))
      continue;
    if (!first)
      output << ",";
    first = false;
    output << "\"" << reason_name(reason) << "\"";
  }
  output << "]";
  return output.str();
}

std::string state(const std::string_view scenario, const PolicyMode mode,
                  const DecisionInput &input) {
  const auto result = evaluate(input);
  std::ostringstream output;
  output << "{\n"
         << "  \"schema\": \"glasswyrm.m14-vrr-state.v1\",\n"
         << "  \"scenario\": \"" << scenario << "\",\n"
         << "  \"policy\": " << static_cast<unsigned>(mode) << ",\n"
         << "  \"decision\": \"" << decision_name(result.decision) << "\",\n"
         << "  \"desired_enabled\": "
         << (result.desired_enabled ? "true" : "false") << ",\n"
         << "  \"candidate_window\": " << result.candidate_window_id << ",\n"
         << "  \"reasons\": " << reason_array(result.reasons) << "\n"
         << "}\n";
  return output.str();
}

std::string fullscreen_state() {
  auto fullscreen = eligible(PolicyMode::Fullscreen);
  const auto full = evaluate(fullscreen);
  fullscreen.candidate.fullscreen = false;
  fullscreen.candidate.borderless_fullscreen = true;
  const auto borderless = evaluate(fullscreen);
  require(full.desired_enabled && borderless.desired_enabled,
          "fullscreen and exact borderless candidates enable simulated VRR");
  return "{\n"
         "  \"schema\": \"glasswyrm.m14-vrr-state.v1\",\n"
         "  \"scenario\": \"fullscreen\",\n"
         "  \"policy\": 2,\n"
         "  \"transitions\": [{\"classification\":\"fullscreen\","
         "\"desired_enabled\":true,\"candidate_window\":41},{"
         "\"classification\":\"borderless-fullscreen\","
         "\"desired_enabled\":true,\"candidate_window\":41}],\n"
         "  \"reasons\": [\"SimulatedHeadless\"]\n"
         "}\n";
}

std::string focused_state() {
  auto first = eligible(PolicyMode::Focused);
  auto second = first;
  second.candidate.window_id = 42;
  second.candidate.surface_id = 52;
  const auto before = evaluate(first);
  const auto after = evaluate(second);
  require(before.desired_enabled && after.desired_enabled &&
              before.candidate_window_id == 41 &&
              after.candidate_window_id == 42,
          "focused policy follows deterministic focus transfer");
  return "{\n"
         "  \"schema\": \"glasswyrm.m14-vrr-state.v1\",\n"
         "  \"scenario\": \"focused\",\n"
         "  \"policy\": 3,\n"
         "  \"candidate_transitions\": [41,42],\n"
         "  \"desired_enabled\": true,\n"
         "  \"reasons\": [\"SimulatedHeadless\"]\n"
         "}\n";
}

std::string app_requested_state() {
  auto input = eligible(PolicyMode::AppRequested);
  const auto default_result = evaluate(input);
  input.candidate.preference = WindowPreference::Prefer;
  const auto prefer_result = evaluate(input);
  input.candidate.preference = WindowPreference::Disable;
  const auto disable_result = evaluate(input);
  require(!default_result.desired_enabled && prefer_result.desired_enabled &&
              !disable_result.desired_enabled,
          "app-requested transitions are off, on, off");
  return "{\n"
         "  \"schema\": \"glasswyrm.m14-vrr-state.v1\",\n"
         "  \"scenario\": \"app-requested\",\n"
         "  \"policy\": 4,\n"
         "  \"preference_transitions\": [{\"preference\":\"Default\","
         "\"desired_enabled\":false,\"primary_reason\":"
         "\"WindowDidNotRequest\"},{\"preference\":\"Prefer\","
         "\"desired_enabled\":true,\"primary_reason\":"
         "\"SimulatedHeadless\"},{\"preference\":\"Disable\","
         "\"desired_enabled\":false,\"primary_reason\":"
         "\"WindowPreferenceDisabled\"}]\n"
         "}\n";
}

std::string reason_fixture() {
  std::ostringstream output;
  output << "{\n"
         << "  \"schema\": \"glasswyrm.m14-vrr-reasons.v1\",\n"
         << "  \"known_mask\": \"0x00000001ffffffff\",\n"
         << "  \"reasons\": [";
  for (std::size_t index = 0; index < kReasonCount; ++index) {
    if (index != 0)
      output << ",";
    output << "{\"bit\":" << index << ",\"name\":\""
           << reason_name(static_cast<Reason>(index)) << "\"}";
  }
  output << "]\n}\n";
  return output.str();
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "open M14 VRR policy fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void current(const std::filesystem::path &root, const std::string_view name,
             const std::string &expected) {
  require(read_file(root / name) == expected,
          "M14 VRR policy fixture is current");
}

} // namespace

int main(const int argc, char **argv) {
  require(argc == 2, "usage: m14_policy_fixture_test FIXTURE_DIR");
  const std::filesystem::path root(argv[1]);

  auto off = eligible(PolicyMode::Off);
  current(root, "vrr-state-off.json", state("off", PolicyMode::Off, off));
  current(root, "vrr-state-fullscreen.json", fullscreen_state());
  current(root, "vrr-state-focused.json", focused_state());
  current(root, "vrr-state-app-requested.json", app_requested_state());
  auto always = eligible(PolicyMode::AlwaysEligible);
  always.candidate.selected = false;
  always.candidate.preference = WindowPreference::Disable;
  current(root, "vrr-state-always.json",
          state("always-eligible", PolicyMode::AlwaysEligible, always));
  current(root, "vrr-reasons.json", reason_fixture());
  return 0;
}
