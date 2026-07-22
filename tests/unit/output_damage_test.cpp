#include "compositor/output_damage.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
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

gwipc_surface_upsert surface(const std::int32_t x, const std::int32_t y = 0,
                             const std::uint32_t width = 2,
                             const std::uint32_t height = 2) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.output_id = 1;
  value.logical_x = x;
  value.logical_y = y;
  value.logical_width = width;
  value.logical_height = height;
  value.visible = true;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.opacity = GWIPC_OPACITY_ONE;
  return value;
}

gw::compositor::SceneDamageResult
exact_damage(std::vector<gw::compositor::Rectangle> rectangles,
             const bool trusted = true,
             const gw::compositor::SurfaceDamageFallbackReason reason =
                 gw::compositor::SurfaceDamageFallbackReason::None) {
  gw::compositor::SceneDamageResult result;
  result.surfaces.emplace(10, gw::compositor::SurfaceDamageState{
                                  std::move(rectangles), trusted, reason});
  return result;
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

gw::compositor::Scene
single_output_scene(const gwipc_output_upsert &native_output,
                    const gwipc_surface_upsert &native_surface) {
  gw::compositor::Scene value;
  value.outputs.emplace(native_output.output_id, native_output);
  value.surfaces.emplace(native_surface.surface_id, native_surface);
  value.surface_outputs.emplace(native_surface.surface_id,
                                membership({native_output.output_id}));
  return value;
}

void test_exact_local_regions() {
  auto native_output = output(1, 0, 2560, 1440);
  auto native_surface = surface(0, 0, 2560, 1440);
  const auto fullscreen = single_output_scene(native_output, native_surface);
  const auto bounded = gw::compositor::calculate_output_damage(
      fullscreen, fullscreen, exact_damage({{100, 200, 64, 64}}));
  gw::test::require(
      bounded.regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{100, 200, 64, 64}},
      "64x64 fullscreen content damage remains exact at 1:1");

  native_surface = surface(100, 100, 100, 100);
  const auto clipped_scene = single_output_scene(native_output, native_surface);
  const auto clipped = gw::compositor::calculate_output_damage(
      clipped_scene, clipped_scene, exact_damage({{-10, -5, 20, 20}}));
  gw::test::require(
      clipped.regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{100, 100, 10, 15}},
      "local damage clips before surface translation");

  native_surface = surface(-32, 0, 64, 64);
  const auto negative = single_output_scene(native_output, native_surface);
  const auto visible = gw::compositor::calculate_output_damage(
      negative, negative, exact_damage({{0, 0, 64, 64}}));
  gw::test::require(
      visible.regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{0, 0, 32, 64}},
      "negative logical origins clip exact damage at the output edge");
}

void test_scale_and_transform_mapping() {
  auto integer_output = output(1, 0, 200, 200, 2, 1);
  auto scaled_surface = surface(0, 0, 100, 100);
  auto scaled_scene = single_output_scene(integer_output, scaled_surface);
  auto scaled = gw::compositor::calculate_output_damage(
      scaled_scene, scaled_scene, exact_damage({{10, 10, 10, 10}}));
  gw::test::require(
      scaled.regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{20, 20, 20, 20}},
      "integer output scale maps exact damage without filter padding");

  auto fractional_output = output(1, 0, 150, 150, 3, 2);
  scaled_scene = single_output_scene(fractional_output, scaled_surface);
  scaled = gw::compositor::calculate_output_damage(
      scaled_scene, scaled_scene, exact_damage({{10, 10, 10, 10}}));
  gw::test::require(
      scaled.regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{14, 14, 17, 17}},
      "fractional bilinear mapping includes its native filter footprint");

  struct TransformCase {
    gwipc_transform transform;
    gw::compositor::Rectangle expected;
  };
  constexpr std::array cases{
      TransformCase{GWIPC_TRANSFORM_NORMAL, {1, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_ROTATE_90, {2, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_ROTATE_180, {2, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_ROTATE_270, {1, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_FLIPPED, {2, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_FLIPPED_90, {2, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_FLIPPED_180, {1, 1, 1, 1}},
      TransformCase{GWIPC_TRANSFORM_FLIPPED_270, {1, 1, 1, 1}},
  };
  for (const auto &item : cases) {
    auto transformed_output = output(1, 0, 4, 3);
    transformed_output.transform = item.transform;
    if (item.transform == GWIPC_TRANSFORM_ROTATE_90 ||
        item.transform == GWIPC_TRANSFORM_ROTATE_270 ||
        item.transform == GWIPC_TRANSFORM_FLIPPED_90 ||
        item.transform == GWIPC_TRANSFORM_FLIPPED_270) {
      transformed_output.logical_width = 3;
      transformed_output.logical_height = 4;
    }
    const auto transformed_scene = single_output_scene(
        transformed_output, surface(0, 0, transformed_output.logical_width,
                                    transformed_output.logical_height));
    const auto transformed = gw::compositor::calculate_output_damage(
        transformed_scene, transformed_scene, exact_damage({{1, 1, 1, 1}}));
    gw::test::require(transformed.regions.at(1) ==
                          std::vector<gw::compositor::Rectangle>{item.expected},
                      "every output transform maps exact local damage");
  }
}

void test_trust_and_structural_fallbacks() {
  auto value = scene(0, {1});
  const auto trusted = gw::compositor::calculate_output_damage(
      value, value, exact_damage({{1, 1, 1, 1}}));
  gw::test::require(
      trusted.regions.at(1) ==
              std::vector<gw::compositor::Rectangle>{{1, 1, 1, 1}} &&
          trusted.fallback_reasons.empty(),
      "trusted stable-buffer damage stays exact");

  const auto replaced = gw::compositor::calculate_output_damage(
      value, value,
      exact_damage(
          {{1, 1, 1, 1}}, false,
          gw::compositor::SurfaceDamageFallbackReason::ReplacementBuffer));
  gw::test::require(
      replaced.regions.at(1) ==
              std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}} &&
          replaced.fallback_reasons.at(1) ==
              std::vector<gw::compositor::OutputDamageFallbackReason>{
                  gw::compositor::OutputDamageFallbackReason::
                      ReplacementBuffer},
      "replacement buffers conservatively invalidate the full surface");
  gw::test::require(
      gw::compositor::calculate_output_damage(value, value, {}).regions.empty(),
      "missing damage on an unchanged stable scene produces no visual change");

  auto hidden = value;
  hidden.surfaces.at(10).visible = false;
  const auto hide = gw::compositor::calculate_output_damage(value, hidden, {});
  const auto show = gw::compositor::calculate_output_damage(hidden, value, {});
  gw::test::require(
      hide.regions.at(1) == show.regions.at(1) &&
          hide.regions.at(1) ==
              std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}},
      "hide and show invalidate the visible structural bounds");

  auto restacked = value;
  restacked.surfaces.at(10).stacking = 99;
  gw::test::require(
      gw::compositor::calculate_output_damage(value, restacked, {})
              .regions.at(1) ==
          std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}},
      "restacking invalidates the surface bounds");

  auto resized = value;
  resized.surfaces.at(10).logical_width = 3;
  const auto resize =
      gw::compositor::calculate_output_damage(value, resized, {});
  gw::test::require(
      resize.regions.at(1) ==
              std::vector<gw::compositor::Rectangle>{{0, 0, 3, 2}} &&
          resize.fallback_reasons.at(1) ==
              std::vector<gw::compositor::OutputDamageFallbackReason>{
                  gw::compositor::OutputDamageFallbackReason::
                      SurfaceStructural},
      "resizing invalidates old and new bounds with a structural reason");

  auto removed = value;
  removed.surfaces.clear();
  removed.surface_outputs.clear();
  gw::test::require(gw::compositor::calculate_output_damage(value, removed, {})
                            .regions.at(1) ==
                        std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}},
                    "removed surfaces invalidate old bounds");
  gw::test::require(gw::compositor::calculate_output_damage(removed, value, {})
                            .regions.at(1) ==
                        std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}},
                    "added surfaces invalidate new bounds");
}

} // namespace

int main() {
  test_exact_local_regions();
  test_scale_and_transform_mapping();
  test_trust_and_structural_fallbacks();
  const auto left = scene(0, {1});
  const auto spanning = scene(3, {1, 2});
  const auto moved = gw::compositor::calculate_output_damage(left, spanning, {});
  gw::test::require(
      moved.regions.size() == 2 && moved.regions.at(1).size() == 2 &&
          moved.regions.at(2).size() == 1 &&
          moved.regions.at(2).front() == gw::compositor::Rectangle{0, 0, 3, 4},
      "old/new memberships map to conservative native damage");

  const auto changed = exact_damage({{0, 0, 2, 2}});
  const auto pixels = gw::compositor::calculate_output_damage(
      spanning, spanning, changed);
  gw::test::require(
      pixels.regions.size() == 2 && !pixels.regions.at(1).empty() &&
          pixels.regions.at(2).front() == gw::compositor::Rectangle{0, 0, 3, 4},
      "content changes damage every current membership");
  gw::test::require(
      gw::compositor::calculate_output_damage(spanning, spanning, {})
          .regions.empty(),
      "unchanged output scene produces no physical damage");

  auto reshaped = spanning;
  reshaped.outputs.at(2) = output(2, 4, 8, 8, 2, 1);
  const auto full =
      gw::compositor::calculate_output_damage(spanning, reshaped, {});
  gw::test::require(
      full.regions.at(2) ==
              std::vector<gw::compositor::Rectangle>{{0, 0, 8, 8}} &&
          full.fallback_reasons.at(2) ==
              std::vector<gw::compositor::OutputDamageFallbackReason>{
                  gw::compositor::OutputDamageFallbackReason::
                      OutputConfiguration},
      "output shape changes force one full physical frame");
}
