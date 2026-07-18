#include "output_client/output_client.hpp"
#include "tests/helpers/test_support.hpp"

#include <sstream>

namespace {

using namespace glasswyrm::tools::output_client;
using gw::test::require;

Snapshot snapshot() {
  Snapshot value;
  value.generation = 7;
  value.primary_output_id = 11;
  value.root_width = 1280;
  value.root_height = 480;
  value.enabled_output_count = 2;
  for (std::uint64_t index = 0; index < 2; ++index) {
    const auto id = 11 + index;
    OutputDescriptor descriptor;
    descriptor.id = id;
    descriptor.kind = GWIPC_OUTPUT_HEADLESS;
    descriptor.name = index == 0 ? "LEFT" : "RIGHT";
    descriptor.capabilities = GWIPC_OUTPUT_CAP_CONNECTED |
                              GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE |
                              GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE |
                              GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE |
                              GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE;
    descriptor.transforms = 0xff;
    descriptor.minimum_scale_numerator = 1;
    descriptor.minimum_scale_denominator = 1;
    descriptor.maximum_scale_numerator = 4;
    descriptor.maximum_scale_denominator = 1;
    descriptor.maximum_scale_denominator_value = 120;
    descriptor.maximum_physical_width = 4096;
    descriptor.maximum_physical_height = 4096;
    value.descriptors.emplace(id, descriptor);
    value.modes.push_back({21 + index, id, 640, 480, 60000, true, true});
    value.outputs.emplace(
        id, OutputState{id, true, static_cast<std::int32_t>(index * 640), 0,
                        640, 480, 640, 480, 60000, 1, 1,
                        GWIPC_TRANSFORM_NORMAL});
  }
  WindowState window;
  window.surface_id = 31;
  window.window_id = 41;
  window.x = 600;
  window.y = 40;
  window.width = 100;
  window.height = 80;
  window.primary_output_id = 12;
  window.output_ids = {11, 12};
  window.preferred_scale_numerator = 5;
  window.preferred_scale_denominator = 4;
  window.client_buffer_scale = 2;
  window.scale_mode = GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  window.visible = window.focused = window.fullscreen = true;
  value.windows.emplace(41, window);
  return value;
}

} // namespace

int main() {
  auto value = snapshot();
  std::ostringstream output;
  print_all(value, true, output);
  const auto json = output.str();
  require(json.starts_with(
              "{\"layout_generation\":7,\"root_width\":1280,"),
          "JSON field order is deterministic");
  require(json.find("\"id\":\"000000000000000b\",\"name\":\"LEFT\"") !=
              std::string::npos,
          "JSON output IDs are lowercase fixed-width hexadecimal");
  require(json.find("\"window_id\":41") != std::string::npos &&
              json.find("\"output_ids\":[\"000000000000000b\","
                        "\"000000000000000c\"]") != std::string::npos &&
              json.find("\"scale_mode\":\"scaled-pixmap\"") !=
                  std::string::npos,
          "JSON includes complete window scaling and membership state");

  Edit edit;
  edit.position = {{640, 0}};
  edit.scale = {{5, 4}};
  std::string error;
  require(apply_edit(value, "RIGHT", edit, error), error);
  require(value.outputs.at(12).logical_width == 512 &&
              value.root_width == 1152,
          "exact scale derives the complete logical layout");

  auto unsupported = snapshot();
  edit = {};
  edit.scale = {{5, 121}};
  require(!apply_edit(unsupported, "RIGHT", edit, error) &&
              error == "requested exact scale is unsupported",
          "unsupported scales are rejected without approximation");

  auto primary = snapshot();
  edit = {};
  edit.enabled = false;
  require(!apply_edit(primary, "LEFT", edit, error) &&
              error.find("primary") != std::string::npos,
          "disabling primary requires another primary selection");

  std::pair<std::uint32_t, std::uint32_t> scale;
  std::pair<std::int32_t, std::int32_t> position;
  std::pair<std::uint32_t, std::uint32_t> extent;
  std::optional<std::uint32_t> refresh;
  require(parse_scale("4/3", scale) && !parse_scale("8/6", scale) &&
              parse_position("12,34", position) &&
              parse_mode("800x600@75000", extent, refresh) &&
              refresh == 75000 && parse_transform("flipped-270"),
          "CLI values parse without scale approximation");
  return 0;
}
