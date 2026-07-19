#include "glasswyrmd/screen_geometry.hpp"

#include "glasswyrmd/server_state.hpp"
#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"
#include "tests/unit/ewmh_test_support.hpp"

#include <cstdint>

namespace {

glasswyrm::output::OutputLayout layout() {
  using namespace glasswyrm::output;
  constexpr OutputId left{11};
  constexpr OutputId right{12};
  constexpr OutputModeId left_mode{21};
  constexpr OutputModeId right_mode{22};
  OutputLayout result;
  OutputDescriptor left_descriptor;
  left_descriptor.id = left;
  left_descriptor.name = "LEFT";
  left_descriptor.connected = true;
  left_descriptor.mode_configurable = true;
  left_descriptor.scale_configurable = true;
  left_descriptor.transform_configurable = true;
  left_descriptor.primary_eligible = true;
  left_descriptor.arbitrary_headless_mode = true;
  left_descriptor.supported_transform_mask = kAllOutputTransformsMask;
  left_descriptor.modes.push_back(
      {left_mode, left, 800, 600, 75'000, 0, "800x600", true, true});
  auto right_descriptor = left_descriptor;
  right_descriptor.id = right;
  right_descriptor.name = "RIGHT";
  right_descriptor.modes = {
      {right_mode, right, 640, 480, 60'000, 0, "640x480", true, true}};
  result.descriptors.emplace(left, std::move(left_descriptor));
  result.descriptors.emplace(right, std::move(right_descriptor));
  OutputState left_state;
  left_state.output_id = left;
  left_state.enabled = true;
  left_state.mode_id = left_mode;
  left_state.logical_width = left_state.physical_width = 800;
  left_state.logical_height = left_state.physical_height = 600;
  left_state.refresh_millihertz = 75'000;
  left_state.scale = {1, 1};
  left_state.primary = true;
  left_state.generation = 4;
  auto right_state = left_state;
  right_state.output_id = right;
  right_state.mode_id = right_mode;
  right_state.logical_x = 800;
  right_state.logical_width = right_state.physical_width = 640;
  right_state.logical_height = right_state.physical_height = 480;
  right_state.refresh_millihertz = 60'000;
  right_state.primary = false;
  result.states.emplace(left, left_state);
  result.states.emplace(right, right_state);
  result.primary_output_id = left;
  result.root_logical_width = 1440;
  result.root_logical_height = 600;
  result.generation = 4;
  result.enabled_output_count = 2;
  result.output_order = {left, right};
  return result;
}

}  // namespace

int main() {
  using glasswyrm::server::derive_output_screen_model;
  using gw::test::require;
  const auto model = derive_output_screen_model(layout());
  require(model && model->width_pixels == 1440 &&
              model->height_pixels == 600 &&
              model->width_millimeters == 381 &&
              model->height_millimeters == 159 &&
              model->refresh_millihertz == 75'000,
          "screen geometry derives root bounds, 96-DPI size, and primary mode");
  require(model->root_window == gw::protocol::x11::kScreenModel.root_window &&
              model->default_colormap ==
                  gw::protocol::x11::kScreenModel.default_colormap &&
              model->root_visual ==
                  gw::protocol::x11::kScreenModel.root_visual &&
              model->root_depth ==
                  gw::protocol::x11::kScreenModel.root_depth &&
              model->red_mask == gw::protocol::x11::kScreenModel.red_mask &&
              model->green_mask ==
                  gw::protocol::x11::kScreenModel.green_mask &&
              model->blue_mask == gw::protocol::x11::kScreenModel.blue_mask &&
              model->maximum_request_length ==
                  gw::protocol::x11::kScreenModel.maximum_request_length &&
              model->resource_id_mask ==
                  gw::protocol::x11::kScreenModel.resource_id_mask,
          "dynamic geometry preserves every fixed X11 identity field");

  auto tie = layout();
  auto& left_mode = tie.descriptors.at(glasswyrm::output::OutputId{11})
                        .modes.front();
  left_mode.physical_width = 120;
  left_mode.physical_height = 240;
  auto& right_mode = tie.descriptors.at(glasswyrm::output::OutputId{12})
                         .modes.front();
  right_mode.physical_width = 120;
  right_mode.physical_height = 240;
  auto& left_state = tie.states.at(glasswyrm::output::OutputId{11});
  left_state.logical_width = left_state.physical_width = 120;
  left_state.logical_height = left_state.physical_height = 240;
  auto& right_state = tie.states.at(glasswyrm::output::OutputId{12});
  right_state.logical_x = 120;
  right_state.logical_width = right_state.physical_width = 120;
  right_state.logical_height = right_state.physical_height = 240;
  tie.root_logical_width = tie.root_logical_height = 240;
  const auto tie_model = derive_output_screen_model(tie);
  require(tie_model && tie_model->width_millimeters == 64 &&
              tie_model->height_millimeters == 64,
          "96-DPI geometry rounds an exact positive half upward");

  glasswyrm::server::ServerState state;
  require(state.update_screen_geometry(*model) &&
              state.screen().width_pixels == 1440 &&
              state.resources().screen().height_pixels == 600 &&
              state.resources().find_window(state.screen().root_window)->width ==
                  1440 &&
              state.resources().find_window(state.screen().root_window)
                      ->height == 600,
          "server and root resource adopt dynamic geometry atomically");
  auto changed_identity = *model;
  ++changed_identity.root_visual;
  require(!state.update_screen_geometry(changed_identity) &&
              state.screen().root_visual ==
                  gw::protocol::x11::kScreenModel.root_visual &&
              state.screen().width_pixels == 1440 &&
              state.resources().screen().width_pixels == 1440 &&
              state.resources().find_window(state.screen().root_window)->width ==
                  1440 &&
              state.resources().find_window(state.screen().root_window)
                      ->height == 600,
          "screen updates reject fixed X11 identity changes without mutation");

  glasswyrm::server::ServerState game_state(
      gw::protocol::x11::kScreenModel, true);
  require(game_state.game_compat() && game_state.update_screen_geometry(*model),
          "game-compatible server adopts output geometry");
  const auto geometry_atom =
      game_state.atoms().find("_NET_DESKTOP_GEOMETRY").value();
  const auto workarea_atom = game_state.atoms().find("_NET_WORKAREA").value();
  require(ewmh_test::property_values(game_state, game_state.screen().root_window,
                                     geometry_atom) ==
                  std::vector<std::uint32_t>({1440, 600}) &&
              ewmh_test::property_values(game_state,
                                         game_state.screen().root_window,
                                         workarea_atom) ==
                  std::vector<std::uint32_t>({0, 0, 1440, 600}),
          "screen update refreshes protected EWMH geometry and work area");

  auto zero_physical = *model;
  zero_physical.width_millimeters = 0;
  require(!state.update_screen_geometry(zero_physical),
          "screen update rejects zero physical geometry");

  auto invalid = layout();
  invalid.generation = 0;
  require(!derive_output_screen_model(invalid),
          "invalid layouts cannot configure the X11 screen");
  return 0;
}
