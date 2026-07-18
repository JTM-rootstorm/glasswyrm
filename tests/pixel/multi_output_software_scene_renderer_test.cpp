#include "backends/output/software_frame_set.hpp"
#include "render/software/multi_output_scene_renderer.hpp"
#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using gw::compositor::SceneModel;
using gw::compositor::SceneProfile;
using gw::render::BufferMappingMap;
using gw::render::SurfaceAttachmentMap;
using gw::render::software::MultiOutputSoftwareSceneRenderer;
using gw::render::software::PhysicalDamageMap;
using gw::render::software::SoftwareFrameSetRenderRequest;

void require(const bool condition, const std::string &message) {
  gw::test::require(condition, message);
}

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

gwipc_output_upsert output(const std::uint64_t id, const std::int32_t x,
                           const std::uint32_t logical_width,
                           const std::uint32_t logical_height,
                           const std::uint32_t physical_width,
                           const std::uint32_t physical_height,
                           const std::uint32_t scale_numerator = 1,
                           const std::uint32_t scale_denominator = 1,
                           const gwipc_transform transform =
                               GWIPC_TRANSFORM_NORMAL) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.enabled = 1;
  value.logical_x = x;
  value.logical_width = logical_width;
  value.logical_height = logical_height;
  value.physical_pixel_width = physical_width;
  value.physical_pixel_height = physical_height;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = scale_numerator;
  value.scale_denominator = scale_denominator;
  value.transform = transform;
  value.color = srgb();
  return value;
}

gwipc_surface_upsert surface(const std::uint64_t id,
                             const std::uint64_t primary_output_id,
                             const std::int32_t x, const std::int32_t y,
                             const std::uint32_t width,
                             const std::uint32_t height,
                             const std::uint32_t client_scale = 1) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = id;
  value.output_id = primary_output_id;
  value.logical_x = x;
  value.logical_y = y;
  value.logical_width = width;
  value.logical_height = height;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = client_scale;
  value.scale_denominator = 1;
  value.color = srgb();
  return value;
}

gwipc_surface_output_state membership(
    const std::uint64_t surface_id, const std::uint64_t primary_output_id,
    const std::vector<std::uint64_t> &outputs,
    const std::uint32_t preferred_numerator,
    const std::uint32_t preferred_denominator,
    const std::uint64_t generation, const std::uint32_t client_scale = 1) {
  gwipc_surface_output_state value{};
  value.struct_size = sizeof(value);
  value.surface_id = surface_id;
  value.primary_output_id = primary_output_id;
  value.output_ids = outputs.data();
  value.output_count = outputs.size();
  value.preferred_scale_numerator = preferred_numerator;
  value.preferred_scale_denominator = preferred_denominator;
  value.client_buffer_scale = client_scale;
  value.scale_mode = client_scale == 1 ? GWIPC_SURFACE_SCALE_LEGACY
                                       : GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  value.layout_generation = generation;
  return value;
}

std::shared_ptr<gw::compositor::BufferMapping>
mapping(const std::uint64_t id, const std::uint32_t width,
        const std::uint32_t height, const std::vector<std::uint32_t> &pixels,
        const gwipc_pixel_format format = GWIPC_PIXEL_FORMAT_XRGB8888) {
  require(pixels.size() == static_cast<std::size_t>(width) * height,
          "mapping fixture dimensions match its pixels");
  const int fd =
      ::memfd_create("multi-output-software-renderer-test",
                     MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(fd >= 0, "create renderer fixture memfd");
  const auto bytes = pixels.size() * sizeof(std::uint32_t);
  require(::ftruncate(fd, static_cast<off_t>(bytes)) == 0 &&
              ::pwrite(fd, pixels.data(), bytes, 0) ==
                  static_cast<ssize_t>(bytes) &&
              ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
          "populate renderer fixture memfd");
  gwipc_buffer_attach attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.buffer_id = id;
  attachment.width = width;
  attachment.height = height;
  attachment.stride = width * 4U;
  attachment.storage_size = bytes;
  attachment.pixel_format = format;
  attachment.alpha_semantics =
      format == GWIPC_PIXEL_FORMAT_XRGB8888 ? GWIPC_ALPHA_OPAQUE
                                            : GWIPC_ALPHA_PREMULTIPLIED;
  attachment.color = srgb();
  std::string error;
  auto imported =
      gw::compositor::BufferMapping::import(attachment, fd, error);
  require(imported != nullptr, error);
  return std::shared_ptr<gw::compositor::BufferMapping>(std::move(imported));
}

void commit(SceneModel &model) {
  require(model.end_complete_snapshot(), "complete output scene validates");
  gwipc_frame_commit frame{};
  frame.struct_size = sizeof(frame);
  frame.commit_id = 1;
  frame.producer_generation = 7;
  require(model.commit(frame).accepted(), "output scene commits");
}

void test_frame_set_bounds_and_historical_visible_hash() {
  glasswyrm::output::SoftwareFrame historical;
  std::string error;
  require(historical.configure(1, 2, 2, error), error);
  historical.pixels()[0] = 0x00112233U;
  historical.pixels()[1] = 0x80445566U;
  historical.pixels()[2] = 0xff778899U;
  historical.pixels()[3] = 0x00aabbccU;
  const auto historical_hash = historical.visible_hash();

  glasswyrm::output::OutputFrameResult frame;
  frame.output = historical.spec(60'000);
  frame.frame = historical;
  frame.damage = {{0, 0, 2, 2}};
  glasswyrm::output::SoftwareFrameSet set;
  require(set.append(std::move(frame), error), error);
  require(set.outputs().at(1).visible_hash == historical_hash &&
              set.outputs().at(1).frame.pixels()[0] == 0x00112233U,
          "frame-set adapter preserves historical visible RGB bytes and hash");
  require(set.finalize(4, 1, 5, 6, 7, error), error);
  require(set.view().valid() &&
              set.aggregate_hash() == UINT64_C(0x12776e1104455a54) &&
              set.aggregate_hash() != historical_hash,
          "aggregate evidence is additive to the historical visible hash");

  glasswyrm::output::SoftwareFrameSet bounded;
  for (std::uint64_t id = 1;
       id <= glasswyrm::output::SoftwareFrameSet::kMaximumOutputs + 1; ++id) {
    glasswyrm::output::OutputFrameResult item;
    require(item.frame.configure(id, 1, 1, error), error);
    item.output = item.frame.spec();
    const bool appended = bounded.append(std::move(item), error);
    require(appended ==
                (id <= glasswyrm::output::SoftwareFrameSet::kMaximumOutputs),
            "frame-set output bound rejects the ninth frame");
  }
}

void test_multi_output_render_and_previous_preservation() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 9), "multi-output snapshot begins");
  require(model.apply(output(1, 0, 2, 2, 2, 2)), "left output stages");
  require(model.apply(output(2, 2, 2, 1, 4, 2, 2, 1)),
          "scaled right output stages");
  require(model.apply(surface(10, 1, 0, 0, 4, 2)),
          "spanning surface stages");
  const std::vector<std::uint64_t> members{1, 2};
  require(model.apply(membership(10, 1, members, 1, 1, 9)),
          "spanning membership stages");
  commit(model);

  BufferMappingMap mappings{{20, mapping(20, 4, 2,
                                          {0xffff0000U, 0xff00ff00U,
                                           0xff0000ffU, 0xffffffffU,
                                           0xff00ffffU, 0xffff00ffU,
                                           0xffffff00U, 0xff101010U})}};
  const SurfaceAttachmentMap attachments{{10, 20}};
  const PhysicalDamageMap full{{1, {{0, 0, 2, 2}}},
                               {2, {{0, 0, 4, 2}}}};
  MultiOutputSoftwareSceneRenderer renderer;
  auto rendered = renderer.render(
      SoftwareFrameSetRenderRequest{model, mappings, attachments, full, nullptr,
                                    11, 12, 13});
  require(rendered.complete() && rendered.frames.outputs().size() == 2,
          rendered.error);
  const std::array<std::uint32_t, 4> left{{
      0xffff0000U, 0xff00ff00U, 0xff00ffffU, 0xffff00ffU}};
  const std::array<std::uint32_t, 8> right{{
      0xff0000ffU, 0xff0000ffU, 0xffffffffU, 0xffffffffU,
      0xff0000ffU, 0xff0000ffU, 0xffffffffU, 0xffffffffU}};
  require(std::ranges::equal(rendered.frames.outputs().at(1).frame.pixels(),
                             left) &&
              std::ranges::equal(
                  rendered.frames.outputs().at(2).frame.pixels(), right),
          "each output renders directly into native physical orientation");
  require(rendered.metrics.at(1).used_direct &&
              rendered.metrics.at(2).used_nearest &&
              !rendered.metrics.at(2).used_bilinear,
          "per-output filtering follows the fixed effective-ratio policy");
  const auto aggregate = rendered.frames.aggregate_hash();
  require(aggregate == UINT64_C(0xe340c21b8eab4293) &&
              rendered.frames.outputs().at(1).visible_hash ==
                  UINT64_C(0xecfd32b513509bef) &&
              rendered.frames.outputs().at(2).visible_hash ==
                  UINT64_C(0x3a1cba140a200a45) &&
              rendered.frames.outputs().at(1).visible_hash ==
                  rendered.frames.outputs().at(1).frame.visible_hash() &&
              rendered.frames.outputs().at(2).visible_hash ==
                  rendered.frames.outputs().at(2).frame.visible_hash(),
          "per-output and aggregate hashes are finalized together");

  mappings.clear();
  mappings.emplace(20, mapping(20, 4, 2, std::vector<std::uint32_t>(8, 0)));
  const PhysicalDamageMap unchanged;
  auto preserved = renderer.render(SoftwareFrameSetRenderRequest{
      model, mappings, attachments, unchanged, &rendered.frames, 14, 15, 16});
  require(preserved.complete() &&
              preserved.frames.aggregate_hash() == aggregate &&
              std::ranges::equal(
                  preserved.frames.outputs().at(2).frame.pixels(), right),
          "compatible prior output frames survive an empty damage transaction");

  const SurfaceAttachmentMap missing;
  auto rejected = renderer.render(SoftwareFrameSetRenderRequest{
      model, mappings, missing, full, &rendered.frames, 17, 18, 19});
  require(rejected.disposition == gw::render::RenderDisposition::InvalidBuffer &&
              rejected.frames.outputs().empty(),
          "one invalid output discards the complete staged frame set");
}

void test_fractional_bilinear_reference() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 2), "fractional snapshot begins");
  require(model.apply(output(1, 0, 4, 1, 5, 1, 5, 4)),
          "fractional output stages");
  require(model.apply(surface(1, 1, 0, 0, 4, 1)),
          "fractional surface stages");
  const std::vector<std::uint64_t> members{1};
  require(model.apply(membership(1, 1, members, 5, 4, 2)),
          "fractional membership stages");
  commit(model);
  const BufferMappingMap mappings{{
      2, mapping(2, 4, 1,
                 {0xff000000U, 0xffff0000U, 0xff00ff00U, 0xff0000ffU})}};
  const SurfaceAttachmentMap attachments{{1, 2}};
  const PhysicalDamageMap damage{{1, {{0, 0, 5, 1}}}};
  MultiOutputSoftwareSceneRenderer renderer;
  const auto rendered = renderer.render(SoftwareFrameSetRenderRequest{
      model, mappings, attachments, damage, nullptr, 3, 4, 5});
  const std::array<std::uint32_t, 5> expected{{
      0xff000000U, 0xffb30000U, 0xff808000U, 0xff00b34dU, 0xff0000ffU}};
  require(rendered.complete() &&
              std::ranges::equal(
                  rendered.frames.outputs().at(1).frame.pixels(), expected) &&
              rendered.metrics.at(1).used_bilinear,
          "fractional sampling uses fixed-point round-half-up bilinear pixels");
}

void test_scaled_client_downsample_reference() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 8), "downsample snapshot begins");
  require(model.apply(output(1, 0, 2, 1, 2, 1)),
          "downsample output stages");
  require(model.apply(surface(1, 1, 0, 0, 2, 1, 2)),
          "scale-aware surface stages");
  const std::vector<std::uint64_t> members{1};
  require(model.apply(membership(1, 1, members, 1, 1, 8, 2)),
          "scale-aware membership stages");
  commit(model);
  const BufferMappingMap mappings{{
      2, mapping(2, 4, 2,
                 {0xff000000U, 0xffff0000U, 0xff00ff00U, 0xff0000ffU,
                  0xff000000U, 0xffff0000U, 0xff00ff00U, 0xff0000ffU})}};
  const SurfaceAttachmentMap attachments{{1, 2}};
  const PhysicalDamageMap damage{{1, {{0, 0, 2, 1}}}};
  MultiOutputSoftwareSceneRenderer renderer;
  const auto rendered = renderer.render(SoftwareFrameSetRenderRequest{
      model, mappings, attachments, damage, nullptr, 9, 10, 11});
  const std::array<std::uint32_t, 2> expected{{0xff800000U, 0xff008080U}};
  require(rendered.complete() &&
              std::ranges::equal(
                  rendered.frames.outputs().at(1).frame.pixels(), expected) &&
              rendered.metrics.at(1).used_bilinear,
          "scaled client downsampling uses canonical bilinear filtering");
}

void test_all_native_output_transforms() {
  struct Case {
    gwipc_transform transform;
    std::uint32_t physical_width;
    std::uint32_t physical_height;
    std::array<std::uint32_t, 6> expected;
  };
  const std::array<Case, 8> cases{{
      {GWIPC_TRANSFORM_NORMAL,
       2,
       3,
       {0xff000001U, 0xff000002U, 0xff000003U, 0xff000004U,
        0xff000005U, 0xff000006U}},
      {GWIPC_TRANSFORM_ROTATE_90,
       3,
       2,
       {0xff000005U, 0xff000003U, 0xff000001U, 0xff000006U,
        0xff000004U, 0xff000002U}},
      {GWIPC_TRANSFORM_ROTATE_180,
       2,
       3,
       {0xff000006U, 0xff000005U, 0xff000004U, 0xff000003U,
        0xff000002U, 0xff000001U}},
      {GWIPC_TRANSFORM_ROTATE_270,
       3,
       2,
       {0xff000002U, 0xff000004U, 0xff000006U, 0xff000001U,
        0xff000003U, 0xff000005U}},
      {GWIPC_TRANSFORM_FLIPPED,
       2,
       3,
       {0xff000002U, 0xff000001U, 0xff000004U, 0xff000003U,
        0xff000006U, 0xff000005U}},
      {GWIPC_TRANSFORM_FLIPPED_90,
       3,
       2,
       {0xff000006U, 0xff000004U, 0xff000002U, 0xff000005U,
        0xff000003U, 0xff000001U}},
      {GWIPC_TRANSFORM_FLIPPED_180,
       2,
       3,
       {0xff000005U, 0xff000006U, 0xff000003U, 0xff000004U,
        0xff000001U, 0xff000002U}},
      {GWIPC_TRANSFORM_FLIPPED_270,
       3,
       2,
       {0xff000001U, 0xff000003U, 0xff000005U, 0xff000002U,
        0xff000004U, 0xff000006U}},
  }};
  const BufferMappingMap mappings{{
      2, mapping(2, 2, 3,
                 {0xff000001U, 0xff000002U, 0xff000003U, 0xff000004U,
                  0xff000005U, 0xff000006U})}};
  const SurfaceAttachmentMap attachments{{1, 2}};
  MultiOutputSoftwareSceneRenderer renderer;
  for (const auto &item : cases) {
    SceneModel model(SceneProfile::OutputModel);
    require(model.begin_complete_snapshot(1, 3), "transform snapshot begins");
    require(model.apply(output(1, 0, 2, 3, item.physical_width,
                               item.physical_height, 1, 1, item.transform)),
            "transformed output stages");
    require(model.apply(surface(1, 1, 0, 0, 2, 3)),
            "transformed surface stages");
    const std::vector<std::uint64_t> members{1};
    require(model.apply(membership(1, 1, members, 1, 1, 3)),
            "transformed membership stages");
    commit(model);
    const PhysicalDamageMap damage{{
        1, {{0, 0, item.physical_width, item.physical_height}}}};
    const auto rendered = renderer.render(SoftwareFrameSetRenderRequest{
        model, mappings, attachments, damage, nullptr, 4, 5, 6});
    require(rendered.complete() &&
                std::ranges::equal(
                    rendered.frames.outputs().at(1).frame.pixels(),
                    item.expected),
            "all eight transforms produce exact native-orientation pixels");
  }
}

} // namespace

int main() {
  test_frame_set_bounds_and_historical_visible_hash();
  test_multi_output_render_and_previous_preservation();
  test_fractional_bilinear_reference();
  test_scaled_client_downsample_reference();
  test_all_native_output_transforms();
}
