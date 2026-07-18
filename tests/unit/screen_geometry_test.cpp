#include "glasswyrmd/screen_geometry.hpp"

#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"

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

  auto invalid = layout();
  invalid.generation = 0;
  require(!derive_output_screen_model(invalid),
          "invalid layouts cannot configure the X11 screen");
  return 0;
}
