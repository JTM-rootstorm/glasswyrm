#include "output_client/output_client.hpp"
#include "output_client/vrr_snapshot.hpp"
#include "tests/helpers/test_support.hpp"

#include <sstream>

namespace {

using namespace glasswyrm::tools::output_client;
using gw::test::require;

Snapshot fixture(const bool controllable = true) {
  Snapshot snapshot;
  snapshot.vrr_queried = true;
  snapshot.generation = 7;
  snapshot.primary_output_id = 0x11;
  OutputDescriptor descriptor;
  descriptor.id = 0x11;
  descriptor.name = "VRR-1";
  snapshot.descriptors.emplace(descriptor.id, descriptor);
  OutputState output;
  output.id = descriptor.id;
  output.enabled = true;
  output.logical_width = 800;
  output.logical_height = 600;
  output.physical_width = 800;
  output.physical_height = 600;
  output.refresh_millihertz = 60'000;
  output.scale_numerator = output.scale_denominator = 1;
  snapshot.outputs.emplace(output.id, output);
  VrrCapability capability;
  capability.output_id = output.id;
  capability.connector_property_present = controllable;
  capability.hardware_capable = controllable;
  capability.kms_controllable = controllable;
  capability.range_available = controllable;
  capability.minimum_refresh_millihertz = controllable ? 48'000 : 0;
  capability.maximum_refresh_millihertz = controllable ? 144'000 : 0;
  snapshot.vrr_capabilities.emplace(output.id, capability);
  snapshot.vrr_policies.emplace(output.id, GWIPC_VRR_POLICY_OFF);
  VrrOutputState state;
  state.output_id = output.id;
  state.requested_mode = GWIPC_VRR_POLICY_OFF;
  state.decision = GWIPC_VRR_DECISION_DISABLED;
  state.reason_flags = GWIPC_VRR_REASON_POLICY_OFF;
  state.state_generation = 8;
  snapshot.vrr_outputs.emplace(output.id, state);
  return snapshot;
}

void test_parsing_and_edit() {
  require(parse_vrr_policy("off") == GWIPC_VRR_POLICY_OFF &&
              parse_vrr_policy("fullscreen") == GWIPC_VRR_POLICY_FULLSCREEN &&
              parse_vrr_policy("focused") == GWIPC_VRR_POLICY_FOCUSED &&
              parse_vrr_policy("app-requested") ==
                  GWIPC_VRR_POLICY_APP_REQUESTED &&
              parse_vrr_policy("always-eligible") ==
                  GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE &&
              !parse_vrr_policy("adaptive"),
          "VRR tool parser accepts only the five frozen modes");
  auto snapshot = fixture();
  std::string error;
  require(
      apply_vrr_edit(snapshot, "VRR-1", GWIPC_VRR_POLICY_FULLSCREEN, error) &&
          snapshot.vrr_policies.at(0x11) == GWIPC_VRR_POLICY_FULLSCREEN,
      "VRR edit selects by stable output name");
  require(apply_vrr_edit(snapshot, "0x0000000000000011",
                         GWIPC_VRR_POLICY_FOCUSED, error) &&
              snapshot.vrr_policies.at(0x11) == GWIPC_VRR_POLICY_FOCUSED,
          "VRR edit selects by fixed-width output ID");
  auto incapable = fixture(false);
  require(
      !apply_vrr_edit(incapable, "VRR-1", GWIPC_VRR_POLICY_FULLSCREEN, error) &&
          error.find("controllable") != std::string::npos,
      "non-off policy exits through the unsupported hardware path");
  require(apply_vrr_edit(incapable, "VRR-1", GWIPC_VRR_POLICY_OFF, error),
          "VRR can always be explicitly disabled");
}

void test_names_and_format() {
  require(vrr_policy_name(GWIPC_VRR_POLICY_APP_REQUESTED) ==
                  std::string_view("app-requested") &&
              vrr_preference_name(GWIPC_VRR_PREFERENCE_PREFER) ==
                  std::string_view("prefer") &&
              vrr_decision_name(GWIPC_VRR_DECISION_REJECTED) ==
                  std::string_view("rejected"),
          "public VRR enums have stable diagnostic names");
  const auto reasons =
      vrr_reason_names(GWIPC_VRR_REASON_OUTPUT_DISABLED |
                       GWIPC_VRR_REASON_MANUAL_ALWAYS_ELIGIBLE);
  require(reasons.size() == 2 && reasons[0] == "output-disabled" &&
              reasons[1] == "manual-always-eligible",
          "reason formatting follows stable bit order");

  auto snapshot = fixture();
  std::ostringstream json;
  print_vrr(snapshot, {}, true, json);
  require(json.str() ==
              "{\"vrr\":[{\"id\":\"0x0000000000000011\",\"name\":\"VRR-1\","
              "\"policy\":\"off\",\"property_present\":true,"
              "\"hardware_capable\":true,\"kms_controllable\":true,"
              "\"simulated\":false,\"range_millihertz\":[48000,144000],"
              "\"decision\":\"disabled\",\"desired_enabled\":false,"
              "\"effective_enabled\":false,\"candidate_window\":0,"
              "\"transition_serial\":0,\"flip_timestamp_monotonic_ns\":0,"
              "\"interval_ns\":0,\"reasons\":[\"policy-off\"]}],"
              "\"windows\":[]}\n",
          "VRR JSON uses deterministic field order and fixed-width IDs");
  std::ostringstream text;
  print_vrr(snapshot, std::string_view("VRR-1"), false, text);
  require(text.str().find(
              "policy=off property_present=1 hardware=1 controllable=1") !=
                  std::string::npos &&
              text.str().find("reasons=[policy-off]") != std::string::npos &&
              text.str().find("flip_timestamp_monotonic_ns=0") !=
                  std::string::npos,
          "VRR text report includes stable decision and reason diagnostics");
}

void test_snapshot_reference_validation() {
  auto snapshot = fixture();
  std::string error;
  require(validate_vrr_snapshot(snapshot, error),
          "complete output-only VRR snapshot validates");

  snapshot.vrr_timings.emplace(0x22, VrrTiming{.output_id = 0x22});
  require(!validate_vrr_snapshot(snapshot, error) &&
              error.find("unknown output") != std::string::npos,
          "VRR timing cannot reference an unknown output");
  snapshot.vrr_timings.clear();

  WindowState window;
  window.surface_id = 0x33;
  window.window_id = 44;
  snapshot.windows.emplace(window.window_id, window);
  require(!validate_vrr_snapshot(snapshot, error) &&
              error.find("omits queried window") != std::string::npos,
          "queried windows require symmetric VRR state");
  snapshot.vrr_windows.emplace(window.window_id,
                               VrrWindowState{.surface_id = window.surface_id,
                                              .window_id = window.window_id,
                                              .output_id = 0x11});
  require(validate_vrr_snapshot(snapshot, error),
          "matching window and output references validate");
}

} // namespace

int main() {
  test_parsing_and_edit();
  test_names_and_format();
  test_snapshot_reference_validation();
  return 0;
}
