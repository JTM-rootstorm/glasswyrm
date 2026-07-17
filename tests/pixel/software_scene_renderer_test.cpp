#include "render/renderer_factory.hpp"
#include "render/software/renderer.hpp"
#include "tests/helpers/test_support.hpp"
#include "config.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

std::shared_ptr<gw::compositor::BufferMapping>
mapping(const std::uint64_t id, const gwipc_pixel_format format,
        const std::array<std::uint32_t, 4>& pixels) {
  const int fd = ::memfd_create("software-scene-renderer-test",
                                MFD_CLOEXEC | MFD_ALLOW_SEALING);
  gw::test::require(fd >= 0, "create renderer test memfd");
  gw::test::require(
      ::ftruncate(fd, static_cast<off_t>(sizeof(pixels))) == 0 &&
          ::pwrite(fd, pixels.data(), sizeof(pixels), 0) ==
              static_cast<ssize_t>(sizeof(pixels)) &&
          ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
      "populate renderer test memfd");
  gwipc_buffer_attach attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.buffer_id = id;
  attachment.width = attachment.height = 2;
  attachment.stride = 8;
  attachment.storage_size = sizeof(pixels);
  attachment.pixel_format = format;
  attachment.alpha_semantics =
      format == GWIPC_PIXEL_FORMAT_XRGB8888 ? GWIPC_ALPHA_OPAQUE
                                            : GWIPC_ALPHA_PREMULTIPLIED;
  attachment.color = srgb();
  std::string error;
  auto imported =
      gw::compositor::BufferMapping::import(attachment, fd, error);
  gw::test::require(imported != nullptr, error);
  return std::shared_ptr<gw::compositor::BufferMapping>(std::move(imported));
}

gw::compositor::Scene scene() {
  gw::compositor::Scene result;
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = output.physical_pixel_width = 2;
  output.logical_height = output.physical_pixel_height = 2;
  output.scale_numerator = output.scale_denominator = 1;
  output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color = srgb();
  result.output = output;
  gwipc_surface_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 10;
  surface.output_id = 1;
  surface.logical_width = surface.logical_height = 2;
  surface.scale_numerator = surface.scale_denominator = 1;
  surface.transform = GWIPC_TRANSFORM_NORMAL;
  surface.opacity = GWIPC_OPACITY_ONE;
  surface.visible = 1;
  surface.color = srgb();
  result.surfaces.emplace(surface.surface_id, surface);
  return result;
}

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-renderer-test-XXXXXX";
  gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                    "create renderer test directory");
  return pattern;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

} // namespace

int main() {
  const auto staged_scene = scene();
  const std::vector<std::uint64_t> stacking{10};
  gw::render::BufferMappingMap mappings;
  mappings.emplace(20, mapping(20, GWIPC_PIXEL_FORMAT_XRGB8888,
                               {0xff102030U, 0xff405060U, 0xff708090U,
                                0xffa0b0c0U}));
  const gw::render::SurfaceAttachmentMap attachments{{10, 20}};
  const std::array<gw::compositor::Rectangle, 1> damage{{0, 0, 1, 1}};
  glasswyrm::output::SoftwareFrame previous;
  std::string error;
  gw::test::require(previous.configure(1, 2, 2, error), error);
  std::ranges::fill(previous.pixels(), 0xffabcdefU);

  std::unique_ptr<gw::render::SceneRenderer> renderer;
  gw::test::require(gw::render::create_scene_renderer(
                        gw::render::RendererRequest::Software, std::nullopt,
                        renderer, error),
                    error);
  const gw::render::RenderFrameRequest request{
      staged_scene, stacking, mappings, attachments, damage, &previous,
      7,            8,        9};
  auto rendered = renderer->render(request);
  gw::test::require(
      rendered.complete() && rendered.metrics.damage_rectangles == 1 &&
          rendered.frame.pixels()[0] == 0xff102030U &&
          rendered.frame.pixels()[1] == 0xff405060U &&
          rendered.frame.pixels()[2] == 0xff708090U &&
          rendered.frame.pixels()[3] == 0xffa0b0c0U,
      "software scene renderer preserves historical intersecting-surface redraw");

  mappings.clear();
  mappings.emplace(20, mapping(20, GWIPC_PIXEL_FORMAT_ARGB8888,
                               {0x407f0000U, 0, 0, 0}));
  rendered = renderer->render(request);
  gw::test::require(
      rendered.disposition == gw::render::RenderDisposition::InvalidBuffer &&
          rendered.error ==
              "ARGB buffer contains a non-premultiplied pixel",
      "software scene renderer preserves premultiplication rejection");

  const auto root = temporary_directory();
  const auto report_path = root / "renderer.jsonl";
  gw::test::require(gw::render::create_scene_renderer(
                        gw::render::RendererRequest::Auto, report_path,
                        renderer, error),
                    error);
  mappings.clear();
  mappings.emplace(20, mapping(20, GWIPC_PIXEL_FORMAT_XRGB8888,
                               {0xff102030U, 0, 0, 0}));
  rendered = renderer->render(request);
  const auto report = read_file(report_path);
  gw::test::require(
      rendered.complete() &&
          report.find("\"requested\":\"auto\"") != std::string::npos &&
#if GW_HAS_GLES_RENDERER
          report.find("\"selected\":\"gles\"") != std::string::npos &&
          report.find("\"egl_platform\":\"surfaceless\"") !=
              std::string::npos &&
          report.find("\"texture_uploads\":1") != std::string::npos &&
#else
          report.find("\"selected\":\"software\"") != std::string::npos &&
          report.find("GLES renderer was not enabled at build time") !=
              std::string::npos &&
#endif
          report.find("\"commit_id\":7") != std::string::npos &&
          report.find("\"damage_rectangles\":1") != std::string::npos,
      "auto selection and frame metrics are reported truthfully");

  std::unique_ptr<gw::render::SceneRenderer> unavailable;
  gw::test::require(
      !gw::render::create_scene_renderer(gw::render::RendererRequest::Software,
                                         report_path, unavailable, error),
      "renderer report path must not already exist");
  std::filesystem::remove(report_path);
  {
    std::ofstream replacement(report_path);
    replacement << "replacement\n";
  }
  rendered = renderer->render(request);
  gw::test::require(
      rendered.disposition == gw::render::RenderDisposition::Fatal &&
          rendered.error == "renderer report target was replaced",
      "renderer report replacement is fatal before presentation");

  gw::test::require(
#if GW_HAS_GLES_RENDERER
      gw::render::create_scene_renderer(gw::render::RendererRequest::Gles,
                                        std::nullopt, unavailable, error),
      "forced GLES initializes when built");
  std::unique_ptr<gw::render::SceneRenderer> software_reference;
  gw::test::require(
      gw::render::create_scene_renderer(gw::render::RendererRequest::Software,
                                        std::nullopt, software_reference, error),
      error);
  previous.pixels()[0] = 0xff000000U;
  previous.pixels()[1] = previous.pixels()[2] = previous.pixels()[3] =
      0xff000000U;
  const auto software_frame = software_reference->render(request);
  const auto gles_frame = unavailable->render(request);
  gw::test::require(
      software_frame.complete() && gles_frame.complete() &&
          std::equal(software_frame.frame.pixels().begin(),
                     software_frame.frame.pixels().end(),
                     gles_frame.frame.pixels().begin()) &&
          gles_frame.metrics.texture_uploads == 1 &&
          gles_frame.metrics.readback_bytes == 4,
      "opaque XRGB software and GLES frames are byte-identical");
#else
      !gw::render::create_scene_renderer(gw::render::RendererRequest::Gles,
                                         std::nullopt, unavailable, error) &&
          error == "GLES renderer was not enabled at build time",
      "forced GLES never falls back to software");
#endif
  const auto real_parent = root / "real-parent";
  const auto linked_parent = root / "linked-parent";
  std::filesystem::create_directory(real_parent);
  std::filesystem::create_directory_symlink(real_parent, linked_parent);
  gw::test::require(
      !gw::render::create_scene_renderer(gw::render::RendererRequest::Software,
                                         linked_parent / "report.jsonl",
                                         unavailable, error) &&
          error == "renderer report parent must be a real directory",
      "renderer report rejects a symbolic-link parent");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
