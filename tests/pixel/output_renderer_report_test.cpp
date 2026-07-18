#include "render/renderer_factory.hpp"
#include "render/renderer_report.hpp"
#include "tests/helpers/test_support.hpp"
#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-output-report-test-XXXXXX";
  gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                    "create output report test directory");
  return pattern;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

gw::compositor::SceneModel output_scene() {
  gw::compositor::SceneModel model(gw::compositor::SceneProfile::OutputModel);
  gw::test::require(model.begin_complete_snapshot(1, 44),
                    "begin report output snapshot");
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = output.logical_height = 2;
  output.physical_pixel_width = output.physical_pixel_height = 3;
  output.refresh_millihertz = 60'000;
  output.scale_numerator = 3;
  output.scale_denominator = 2;
  output.transform = GWIPC_TRANSFORM_ROTATE_180;
  output.color = srgb();
  gw::test::require(model.apply(output) && model.end_complete_snapshot(),
                    "stage report output snapshot");
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 7;
  commit.producer_generation = 8;
  gw::test::require(model.commit(commit).accepted(),
                    "commit report output snapshot");
  return model;
}

void test_historical_bytes(const std::filesystem::path& root) {
  const auto path = root / "historical.jsonl";
  gw::render::RendererReport report(path);
  gw::render::RendererSelectionReport selection;
  selection.requested = gw::render::RendererRequest::Software;
  std::string error;
  gw::test::require(report.initialize(selection, error), error);

  gw::compositor::Scene scene;
  const std::vector<std::uint64_t> stacking;
  const gw::render::BufferMappingMap mappings;
  const gw::render::SurfaceAttachmentMap attachments;
  const std::vector<gw::compositor::Rectangle> damage;
  const gw::render::RenderFrameRequest request{
      scene, stacking, mappings, attachments, damage, nullptr, 7, 8, 9};
  gw::render::RenderFrameResult result;
  result.disposition = gw::render::RenderDisposition::Complete;
  result.metrics = {3, 4, 5, 6, 7};
  gw::test::require(report.append_frame(request, result, "software", error),
                    error);

  const std::string expected =
      "{\"record\":\"selection\",\"requested\":\"software\","
      "\"selected\":\"software\",\"egl_platform\":null,\"egl_vendor\":null,"
      "\"egl_version\":null,\"gles_version\":null,\"gl_vendor\":null,"
      "\"gl_renderer\":null,\"gl_version\":null,\"gbm_device\":null,"
      "\"render_node\":null,\"software_renderer\":true,"
      "\"fallback_reasons\":[]}\n"
      "{\"record\":\"frame\",\"selected\":\"software\",\"commit_id\":7,"
      "\"generation\":8,\"ordinal\":9,\"disposition\":\"complete\","
      "\"texture_uploads\":4,\"texture_upload_bytes\":5,"
      "\"damage_rectangles\":3,\"readback_bytes\":7,"
      "\"texture_cache_bytes\":6,\"fallback_reason\":null,\"error\":null}\n";
  gw::test::require(read_file(path) == expected,
                    "historical renderer report remains byte-identical");
}

void test_output_metrics(const std::filesystem::path& root) {
  const auto path = root / "output.jsonl";
  std::unique_ptr<gw::render::OutputSceneRenderer> renderer;
  std::string error;
  const gw::render::RendererCreateOptions options{
      gw::render::RendererRequest::Software, path, std::nullopt,
      gw::render::kMaximumGlTextureCacheBytes};
  gw::test::require(
      gw::render::create_output_scene_renderer(options, renderer, error),
      error);
  auto scene = output_scene();
  const gw::render::BufferMappingMap mappings;
  const gw::render::SurfaceAttachmentMap attachments;
  const gw::render::software::PhysicalDamageMap damage{
      {1, {{0, 0, 3, 3}}}};
  const gw::render::software::SoftwareFrameSetRenderRequest request{
      scene, mappings, attachments, damage, nullptr, 7, 8, 9};
  const auto result = renderer->render(request);
  gw::test::require(result.complete() && result.metrics.size() == 1,
                    result.error);

  const auto contents = read_file(path);
  const auto output_record = contents.find("{\"record\":\"output-frame\"");
  gw::test::require(
      output_record != std::string::npos &&
          contents.find("\"schema_version\":13", output_record) !=
              std::string::npos &&
          contents.find("\"output_id\":\"0000000000000001\"", output_record) !=
              std::string::npos &&
          contents.find("\"texture_uploads\":0", output_record) !=
              std::string::npos &&
          contents.find("\"texture_upload_bytes\":0", output_record) !=
              std::string::npos &&
          contents.find("\"physical_damage_rectangles\":[{\"x\":0,\"y\":0,"
                        "\"width\":3,\"height\":3}]",
                        output_record) != std::string::npos &&
          contents.find("\"readback_bytes\":0", output_record) !=
              std::string::npos &&
          contents.find("\"texture_cache_bytes\":0", output_record) !=
              std::string::npos &&
          contents.find("\"filtering_modes\":[]", output_record) !=
              std::string::npos &&
          contents.find("\"scale_numerator\":3,\"scale_denominator\":2",
                        output_record) != std::string::npos &&
          contents.find("\"transform\":\"rotate-180\"", output_record) !=
              std::string::npos &&
          contents.find("\"fallback_reason\":null", output_record) !=
              std::string::npos &&
          contents.find("\"maximum_fractional_comparison_error\":0",
                        output_record) != std::string::npos,
      "output renderer report contains deterministic per-output metrics");
}

void test_accelerated_metric_serialization(const std::filesystem::path& root) {
  const auto path = root / "accelerated.jsonl";
  gw::render::RendererReport report(path);
  gw::render::RendererSelectionReport selection;
  selection.requested = gw::render::RendererRequest::Gles;
  selection.selected = "gles";
  selection.software_renderer = false;
  std::string error;
  gw::test::require(report.initialize(selection, error), error);

  auto scene = output_scene();
  const gw::render::BufferMappingMap mappings;
  const gw::render::SurfaceAttachmentMap attachments;
  const gw::render::software::PhysicalDamageMap damage;
  const gw::render::software::SoftwareFrameSetRenderRequest request{
      scene, mappings, attachments, damage, nullptr, 7, 8, 9};
  gw::render::OutputSceneRenderResult result;
  result.disposition = gw::render::RenderDisposition::Complete;
  result.selected_renderer = "gles";
  result.fallback_reason = "frame-set fallback";
  glasswyrm::output::OutputFrameResult frame;
  frame.output = {1, 3, 3, 60'000};
  frame.logical = {0, 0, 3, 3};
  frame.scale = {5, 4};
  frame.transform = glasswyrm::output::OutputTransform::Flipped270;
  gw::test::require(frame.frame.configure(1, 3, 3, error), error);
  frame.damage = {{1, 0, 2, 3}};
  gw::test::require(result.frames.append(std::move(frame), error) &&
                        result.frames.finalize(44, 1, 7, 8, 9, error),
                    error);
  gw::render::OutputRendererMetrics metrics;
  metrics.texture_uploads = 11;
  metrics.texture_upload_bytes = 12;
  metrics.physical_damage_rectangles = {{1, 0, 2, 3}};
  metrics.readback_bytes = 13;
  metrics.texture_cache_bytes = 14;
  metrics.scale = {5, 4};
  metrics.transform = glasswyrm::output::OutputTransform::Flipped270;
  metrics.fallback_reason = "frame-set fallback";
  metrics.maximum_fractional_comparison_error = 1;
  metrics.used_nearest = true;
  metrics.used_bilinear = true;
  result.metrics.emplace(1, std::move(metrics));
  gw::test::require(report.append_output_frame(request, result, error), error);

  const auto contents = read_file(path);
  const auto record = contents.find("{\"record\":\"output-frame\"");
  gw::test::require(
      record != std::string::npos &&
          contents.find("\"texture_uploads\":11,\"texture_upload_bytes\":12",
                        record) != std::string::npos &&
          contents.find("\"readback_bytes\":13,\"texture_cache_bytes\":14",
                        record) != std::string::npos &&
          contents.find("\"filtering_modes\":[\"nearest\",\"bilinear\"]",
                        record) != std::string::npos &&
          contents.find("\"scale_numerator\":5,\"scale_denominator\":4",
                        record) != std::string::npos &&
          contents.find("\"transform\":\"flipped-270\"", record) !=
              std::string::npos &&
          contents.find("\"fallback_reason\":\"frame-set fallback\"",
                        record) != std::string::npos &&
          contents.find("\"maximum_fractional_comparison_error\":1", record) !=
              std::string::npos,
      "accelerated output metrics retain their exact diagnostic values");
}

#if !GW_HAS_GLES_RENDERER
void test_auto_startup_fallback(const std::filesystem::path& root) {
  const auto path = root / "auto-fallback.jsonl";
  std::unique_ptr<gw::render::OutputSceneRenderer> renderer;
  std::string error;
  const gw::render::RendererCreateOptions options{
      gw::render::RendererRequest::Auto, path, std::nullopt,
      gw::render::kMaximumGlTextureCacheBytes};
  gw::test::require(
      gw::render::create_output_scene_renderer(options, renderer, error),
      error);
  auto scene = output_scene();
  const gw::render::BufferMappingMap mappings;
  const gw::render::SurfaceAttachmentMap attachments;
  const gw::render::software::PhysicalDamageMap damage;
  const gw::render::software::SoftwareFrameSetRenderRequest request{
      scene, mappings, attachments, damage, nullptr, 7, 8, 9};
  const auto result = renderer->render(request);
  constexpr std::string_view reason =
      "GLES renderer was not enabled at build time";
  gw::test::require(
      result.complete() && result.fallback_reason == reason &&
          result.metrics.at(1).fallback_reason == reason &&
          read_file(path).find("\"fallback_reason\":\"" +
                               std::string(reason) + "\"") !=
              std::string::npos,
      "auto startup fallback is preserved in output frame diagnostics");
}
#endif

} // namespace

int main() {
  const auto root = temporary_directory();
  test_historical_bytes(root);
  test_output_metrics(root);
  test_accelerated_metric_serialization(root);
#if !GW_HAS_GLES_RENDERER
  test_auto_startup_fallback(root);
#endif
  std::filesystem::remove_all(root);
}
