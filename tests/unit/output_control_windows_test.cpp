#include "glasswyrmd/output_control_windows.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

using namespace glasswyrm;
using gw::test::require;

output::OutputLayout layout() {
  output::OutputLayout value;
  const output::OutputId id{11};
  const output::OutputModeId mode_id{21};
  output::OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "LEFT";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
  descriptor.modes.push_back(
      {mode_id, id, 640, 480, 60000, 0, "640x480", true, true});
  value.descriptors.emplace(id, std::move(descriptor));
  output::OutputState state;
  state.output_id = id;
  state.mode_id = mode_id;
  state.enabled = true;
  state.logical_width = state.physical_width = 640;
  state.logical_height = state.physical_height = 480;
  state.refresh_millihertz = 60000;
  state.scale = {1, 1};
  state.primary = true;
  state.generation = 7;
  value.states.emplace(id, state);
  value.output_order = {id};
  value.primary_output_id = id;
  value.root_logical_width = 640;
  value.root_logical_height = 480;
  value.generation = 7;
  value.enabled_output_count = 1;
  return value;
}

server::LifecycleSnapshot windows() {
  server::LifecycleSnapshot value;
  server::LifecycleWindow window;
  window.xid = 41;
  window.applied_x = 10;
  window.applied_y = 20;
  window.applied_width = 100;
  window.applied_height = 80;
  window.stacking = 0;
  window.policy_visible = true;
  window.focused = true;
  window.window_type = GWIPC_POLICY_WINDOW_DIALOG;
  window.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  window.managed = true;
  window.decoration_eligible = true;
  window.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  window.assigned_output_id = 11;
  window.output_memberships = {11};
  window.scale.has_output_state = true;
  window.scale.preferred_scale_numerator = 5;
  window.scale.preferred_scale_denominator = 4;
  window.scale.accepted_buffer_scale = 2;
  window.scale.presentation =
      server::WindowScalePresentationState::ScaleAwareActive;
  value.windows.emplace(41, std::move(window));
  return value;
}

} // namespace

int main() {
  const auto records =
      server::build_output_control_windows(windows(), layout());
  require(records && records->size() == 1,
          "server builds one deterministic control window record");
  const auto &record = records->front();
  require(record.surface.x11_window_id == 41 &&
              record.surface.logical_x == 10 &&
              record.surface.logical_width == 100 &&
              record.surface.output_id == 11 &&
              record.surface.scale_numerator == 2 &&
              record.surface.presentation_flags ==
                  GWIPC_SURFACE_PRESENTATION_METADATA_ONLY,
          "control surface preserves geometry, primary output, and scale");
  require(record.policy.applied_state == GWIPC_POLICY_APPLIED_FULLSCREEN &&
              record.policy.focused && record.policy.managed,
          "control policy preserves focus and fullscreen state");
  require(record.output_ids == std::vector<std::uint64_t>{11} &&
              record.membership.preferred_scale_numerator == 5 &&
              record.membership.preferred_scale_denominator == 4 &&
              record.membership.client_buffer_scale == 2 &&
              record.membership.layout_generation == 7,
          "control membership preserves complete scaling metadata");
  return 0;
}
