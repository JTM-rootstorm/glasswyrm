#include "glasswyrmd/lifecycle_projection.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>

using namespace glasswyrm;
using namespace glasswyrm::server;

namespace {

constexpr output::OutputId kLeft{10};
constexpr output::OutputId kRight{20};

void require(const bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "lifecycle projection: %s\n", message);
    std::exit(1);
  }
}

output::OutputLayout output_layout() {
  output::OutputLayout layout;
  layout.generation = 7;
  layout.primary_output_id = kLeft;
  layout.root_logical_width = 180;
  layout.root_logical_height = 100;
  layout.enabled_output_count = 2;
  layout.output_order = {kLeft, kRight};

  const auto add = [&](const output::OutputId output_id,
                       const output::OutputModeId mode_id, const char* name,
                       const std::int32_t x, const std::uint32_t logical_width,
                       const std::uint32_t physical_width,
                       const std::uint32_t physical_height,
                       const output::RationalScale scale,
                       const bool primary) {
    output::OutputDescriptor descriptor;
    descriptor.id = output_id;
    descriptor.name = name;
    descriptor.connected = true;
    descriptor.mode_configurable = true;
    descriptor.scale_configurable = true;
    descriptor.primary_eligible = true;
    descriptor.arbitrary_headless_mode = true;
    descriptor.modes.push_back({mode_id, output_id, physical_width,
                                physical_height, 60'000, 0, name, true, true});
    layout.descriptors.emplace(output_id, std::move(descriptor));

    output::OutputState state;
    state.output_id = output_id;
    state.enabled = true;
    state.mode_id = mode_id;
    state.logical_x = x;
    state.logical_width = logical_width;
    state.logical_height = 100;
    state.physical_width = physical_width;
    state.physical_height = physical_height;
    state.refresh_millihertz = 60'000;
    state.scale = scale;
    state.primary = primary;
    state.generation = layout.generation;
    layout.states.emplace(output_id, state);
  };
  add(kLeft, output::OutputModeId{11}, "LEFT", 0, 100, 100, 100, {1, 1},
      true);
  add(kRight, output::OutputModeId{21}, "RIGHT", 100, 80, 160, 200, {2, 1},
      false);
  return layout;
}

LifecycleWindow window(const std::uint32_t xid,
                       const std::uint64_t serial) {
  LifecycleWindow result;
  result.xid = xid;
  result.parent = 77;
  result.window_class = WindowClass::InputOutput;
  result.requested_width = 100;
  result.requested_height = 50;
  result.creation_serial = serial;
  return result;
}

gwipc_policy_window_state policy_state(const std::uint32_t xid) {
  gwipc_policy_window_state state{};
  state.struct_size = sizeof(state);
  state.window_id = xid;
  state.workspace_id = 3;
  state.output_id = 9;
  state.final_width = 100;
  state.final_height = 50;
  state.stacking = -1;
  state.window_type = GWIPC_POLICY_WINDOW_DIALOG;
  state.applied_state = GWIPC_POLICY_APPLIED_MINIMIZED;
  state.managed = 1;
  state.decoration_eligible = 1;
  state.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  state.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return state;
}

void test_historical_projection() {
  LifecycleSnapshot proposed;
  proposed.root_window = 77;
  proposed.workspace_id = 3;
  proposed.output_id = 9;
  proposed.focused_window = 77;
  proposed.root_order = {999, 10, 20};
  auto a = window(10, 1);
  a.transient_for = 9;
  a.policy_window_type = PolicyWindowType::Utility;
  a.decoration_preference = PolicyDecoration::False;
  a.above_requested = true;
  a.bypass_compositor = true;
  a.input_requested = false;
  a.attention_requested = true;
  a.minimum_width = 120;
  a.maximum_width = 150;
  a.minimum_height = 40;
  a.maximum_height = 45;
  auto b = a;
  b.xid = 20;
  b.creation_serial = 2;
  proposed.windows.emplace(10, a);
  proposed.windows.emplace(20, b);

  PolicySnapshotResult result;
  result.generation = 4;
  auto hidden = policy_state(10);
  auto visible = hidden;
  visible.window_id = 20;
  visible.final_x = 12;
  visible.final_y = 14;
  visible.stacking = 0;
  visible.visible = 1;
  visible.focused = 1;
  visible.window_type = GWIPC_POLICY_WINDOW_UTILITY;
  visible.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  visible.attention_requested = 1;
  visible.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  result.windows = {visible, hidden};
  auto evaluated = apply_policy_result(proposed, result);
  require(evaluated && evaluated->focused_window == 20 &&
              evaluated->root_order ==
                  std::vector<std::uint32_t>({999, 10, 20}),
          "focus and hidden/nonpolicy root order retained");
  const auto& stored = evaluated->windows.at(20);
  require(stored.window_type == GWIPC_POLICY_WINDOW_UTILITY &&
              stored.applied_state == GWIPC_POLICY_APPLIED_FULLSCREEN &&
              stored.managed && stored.decoration_eligible &&
              stored.attention_requested &&
              stored.fullscreen_eligible == GWIPC_TRI_STATE_TRUE,
          "exact returned metadata retained");

  const auto compositor = project_compositor(*evaluated, 5, 4);
  require(compositor.surfaces.size() == 2 &&
              compositor.policies.size() == 2 &&
              compositor.surfaces.at(1).output_id == 9 &&
              compositor.policies.at(1).workspace_id == 3 &&
              compositor.policies.at(1).window_type ==
                  GWIPC_POLICY_WINDOW_UTILITY &&
              compositor.policies.at(1).applied_state ==
                  GWIPC_POLICY_APPLIED_FULLSCREEN &&
              compositor.policies.at(1).attention_requested == 1 &&
              compositor.outputs.empty() &&
              compositor.surface_outputs.empty(),
          "historical compositor projection remains byte-shaped exactly");
  const auto buffered = project_compositor(*evaluated, 6, 5, true);
  require(buffered.surfaces.at(1).presentation_flags == 0 &&
              buffered.surface_outputs.empty(),
          "historical software content does not grow output-model records");

  result.windows.at(0).final_width = UINT32_MAX;
  require(!apply_policy_result(proposed, result),
          "unrepresentable geometry rejected before compositor");
  result.windows.at(0) = visible;
  result.windows.at(0).visible = 2;
  require(!apply_policy_result(proposed, result),
          "invalid boolean rejected");

  const auto policy = project_policy(proposed, 8, 9);
  require(policy.windows.size() == 2 && policy.outputs.empty() &&
              policy.output_hints.empty() &&
              policy.windows.at(0).window.parent_window_id == 77 &&
              policy.windows.at(0).window.workspace_id == 3,
          "historical policy projection does not grow output records");
  const auto& wire = policy.windows.at(0).window;
  require(wire.transient_for == 9 &&
              wire.window_type == GWIPC_POLICY_WINDOW_UTILITY &&
              wire.decoration_preference == GWIPC_TRI_STATE_FALSE &&
              wire.attention_requested == 1 && wire.requested_width == 120 &&
              wire.requested_height == 45 &&
              wire.flags ==
                  (GWIPC_POLICY_WINDOW_FLAG_ABOVE |
                   GWIPC_POLICY_WINDOW_FLAG_BYPASS_COMPOSITOR |
                   GWIPC_POLICY_WINDOW_FLAG_INPUT_DISABLED),
          "historical EWMH and policy fields retain their wire values");
}

void test_output_model_projection() {
  const auto layout = output_layout();
  LifecycleSnapshot proposed;
  proposed.root_window = 77;
  proposed.workspace_id = 3;
  proposed.focused_window = 77;
  proposed.root_order = {10, 20};
  auto spanning = window(10, 1);
  spanning.assigned_output_id = kLeft.value;
  spanning.applied_width = 30;
  spanning.applied_height = 20;
  spanning.scale.presentation = WindowScalePresentationState::ScaleAwareActive;
  spanning.scale.accepted_buffer_scale = 2;
  auto offscreen = window(20, 2);
  offscreen.override_redirect = true;
  offscreen.assigned_output_id = kRight.value;
  proposed.windows.emplace(spanning.xid, spanning);
  proposed.windows.emplace(offscreen.xid, offscreen);

  const auto policy = project_policy(proposed, 8, 9, &layout);
  require(policy.outputs.size() == 2 && policy.output_hints.size() == 2 &&
              policy.outputs.at(0).output_id == kLeft.value &&
              policy.outputs.at(1).output_id == kRight.value &&
              policy.output_hints.at(0).previous_output_id == kLeft.value &&
              policy.output_hints.at(1).previous_output_id == kRight.value,
          "output-model policy carries complete topology and prior primaries");

  auto left_right = policy_state(spanning.xid);
  left_right.output_id = kRight.value;
  left_right.final_x = 90;
  left_right.final_y = 10;
  left_right.final_width = 30;
  left_right.final_height = 20;
  left_right.stacking = 0;
  left_right.visible = 1;
  left_right.focused = 1;
  auto outside = policy_state(offscreen.xid);
  outside.output_id = kRight.value;
  outside.final_x = 300;
  outside.final_y = 10;
  outside.final_width = 20;
  outside.final_height = 20;
  outside.stacking = 1;
  outside.visible = 1;
  outside.override_redirect = 1;
  outside.managed = 0;
  PolicySnapshotResult result;
  result.generation = 9;
  result.windows = {left_right, outside};
  const auto evaluated = apply_policy_result(proposed, result, &layout);
  require(evaluated &&
              evaluated->windows.at(spanning.xid).assigned_output_id ==
                  kRight.value &&
              evaluated->windows.at(spanning.xid).output_memberships ==
                  std::vector<std::uint64_t>({kLeft.value, kRight.value}) &&
              evaluated->windows.at(spanning.xid)
                      .scale.preferred_scale_numerator == 2 &&
              evaluated->windows.at(spanning.xid).scale.layout_generation ==
                  layout.generation,
          "spanning surface membership follows canonical logical layout order");
  require(evaluated->windows.at(offscreen.xid).assigned_output_id ==
              kRight.value &&
              evaluated->windows.at(offscreen.xid).output_memberships.empty(),
          "offscreen override-redirect surface retains its assigned primary");

  auto resized_result = result;
  resized_result.windows.at(0).final_width = 31;
  const auto resized = apply_policy_result(proposed, resized_result, &layout);
  require(resized &&
              resized->windows.at(spanning.xid).scale.presentation ==
                  WindowScalePresentationState::ScaleAwareAwaitingPixmap &&
              !resized->windows.at(spanning.xid).scale.scaled_pixmap_storage,
          "logical resize invalidates active scaled-pixmap presentation");

  const auto compositor =
      project_compositor(*evaluated, 10, 11, true, &layout);
  require(compositor.generation == 11 && compositor.outputs.size() == 2 &&
              compositor.surfaces.size() == 2 &&
              compositor.surface_outputs.size() == 2 &&
              compositor.surface_outputs.at(0).state.surface_id ==
                  compositor.surfaces.at(0).surface_id &&
              compositor.surface_outputs.at(0).state.primary_output_id ==
                  kRight.value &&
              compositor.surface_outputs.at(0).output_ids ==
                  std::vector<std::uint64_t>({kLeft.value, kRight.value}) &&
              compositor.surface_outputs.at(0).state.output_count == 2 &&
              compositor.surface_outputs.at(0).state.client_buffer_scale == 2 &&
              compositor.surface_outputs.at(0).state.scale_mode ==
                  GWIPC_SURFACE_SCALE_SCALED_PIXMAP &&
              compositor.surface_outputs.at(1).state.primary_output_id ==
                  kRight.value &&
              compositor.surface_outputs.at(1).output_ids.empty() &&
              compositor.surface_outputs.at(1).state.output_count == 0,
          "every nonmetadata surface owns one complete output-state record");

  const auto metadata =
      project_compositor(*evaluated, 12, 13, false, &layout);
  require(metadata.outputs.size() == 2 && metadata.surfaces.size() == 2 &&
              metadata.surface_outputs.empty(),
          "metadata-only output-model scenes carry topology without membership");

  result.windows.at(0).output_id = 999;
  require(!apply_policy_result(proposed, result, &layout),
          "policy cannot assign a window to an unknown output");
}

}  // namespace

int main() {
  test_historical_projection();
  test_output_model_projection();
  return 0;
}
