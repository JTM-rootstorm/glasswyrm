#include "compositor/output_damage.hpp"
#include "tests/helpers/test_support.hpp"

#include <vector>

namespace {

gwipc_output_upsert output(const std::uint64_t id, const std::int32_t x,
                           const std::uint32_t physical_width,
                           const std::uint32_t physical_height,
                           const std::uint32_t scale_numerator = 1,
                           const std::uint32_t scale_denominator = 1) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.enabled = true;
  value.logical_x = x;
  value.logical_width =
      (physical_width * scale_denominator + scale_numerator - 1U) /
      scale_numerator;
  value.logical_height =
      (physical_height * scale_denominator + scale_numerator - 1U) /
      scale_numerator;
  value.physical_pixel_width = physical_width;
  value.physical_pixel_height = physical_height;
  value.scale_numerator = scale_numerator;
  value.scale_denominator = scale_denominator;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  return value;
}

gwipc_surface_upsert surface(const std::int32_t x) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.output_id = 1;
  value.logical_x = x;
  value.logical_width = 2;
  value.logical_height = 2;
  value.visible = true;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.opacity = GWIPC_OPACITY_ONE;
  return value;
}

gw::compositor::SurfaceOutputMembership membership(
    std::vector<std::uint64_t> outputs) {
  return {outputs.front(), std::move(outputs), 1, 1, 1,
          GWIPC_SURFACE_SCALE_LEGACY, 9, 0};
}

gw::compositor::Scene scene(const std::int32_t surface_x,
                            std::vector<std::uint64_t> outputs) {
  gw::compositor::Scene value;
  value.outputs.emplace(1, output(1, 0, 4, 4));
  value.outputs.emplace(2, output(2, 4, 5, 5, 5, 4));
  value.surfaces.emplace(10, surface(surface_x));
  value.surface_outputs.emplace(10, membership(std::move(outputs)));
  return value;
}

} // namespace

int main() {
  const auto left = scene(0, {1});
  const auto spanning = scene(3, {1, 2});
  const auto moved = gw::compositor::calculate_output_damage(left, spanning, {});
  gw::test::require(moved.size() == 2 && moved.at(1).size() == 2 &&
                        moved.at(2).size() == 1 &&
                        moved.at(2).front() ==
                            gw::compositor::Rectangle{0, 0, 3, 4},
                    "old/new memberships map to conservative native damage");

  const std::vector<std::uint64_t> changed{10};
  const auto pixels = gw::compositor::calculate_output_damage(
      spanning, spanning, changed);
  gw::test::require(pixels.size() == 2 && !pixels.at(1).empty() &&
                        pixels.at(2).front() ==
                            gw::compositor::Rectangle{0, 0, 3, 4},
                    "content changes damage every current membership");
  gw::test::require(
      gw::compositor::calculate_output_damage(spanning, spanning, {}).empty(),
      "unchanged output scene produces no physical damage");

  auto reshaped = spanning;
  reshaped.outputs.at(2) = output(2, 4, 8, 8, 2, 1);
  const auto full =
      gw::compositor::calculate_output_damage(spanning, reshaped, {});
  gw::test::require(full.at(2) ==
                        std::vector<gw::compositor::Rectangle>{{0, 0, 8, 8}},
                    "output shape changes force one full physical frame");
}
