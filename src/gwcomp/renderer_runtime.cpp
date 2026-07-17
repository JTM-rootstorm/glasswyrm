#include "gwcomp/renderer_runtime.hpp"

#include "render/renderer_factory.hpp"

#include <filesystem>
#include <algorithm>
#include <optional>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <vector>

namespace {

std::optional<std::filesystem::path>
validated_character_node(const std::filesystem::path& path) {
  struct stat status {};
  if (::lstat(path.c_str(), &status) != 0 || !S_ISCHR(status.st_mode) ||
      S_ISLNK(status.st_mode))
    return std::nullopt;
  return path;
}

std::optional<std::filesystem::path>
associated_render_node(const dev_t device) {
  const auto directory = std::filesystem::path("/sys/dev/char") /
      (std::to_string(major(device)) + ":" + std::to_string(minor(device))) /
      "device/drm";
  std::vector<std::filesystem::path> candidates;
  std::error_code error;
  for (std::filesystem::directory_iterator entries(directory, error), end;
       !error && entries != end; entries.increment(error)) {
    const auto name = entries->path().filename().string();
    if (!name.starts_with("renderD") || name.size() == 7 ||
        !std::all_of(name.begin() + 7, name.end(), [](const unsigned char c) {
          return c >= '0' && c <= '9';
        }))
      continue;
    const auto node = validated_character_node(
        std::filesystem::path("/dev/dri") / name);
    if (node) candidates.push_back(*node);
  }
  return candidates.size() == 1
      ? std::optional<std::filesystem::path>(candidates.front())
      : std::nullopt;
}

std::optional<std::filesystem::path>
validated_render_node(const glasswyrm::compositor::Options& options) {
  if (options.drm_fd) {
    struct stat status {};
    return ::fstat(*options.drm_fd, &status) == 0 && S_ISCHR(status.st_mode)
               ? associated_render_node(status.st_rdev)
               : std::nullopt;
  }
  if (options.drm_device && *options.drm_device != "auto") {
    struct stat status {};
    if (::stat(options.drm_device->c_str(), &status) == 0 &&
        S_ISCHR(status.st_mode))
      return associated_render_node(status.st_rdev);
    return std::nullopt;
  }
  if (options.backend != glasswyrm::compositor::Backend::Headless)
    return std::nullopt;
  std::vector<std::filesystem::path> candidates;
  std::error_code error;
  for (std::filesystem::directory_iterator entries("/dev/dri", error), end;
       !error && entries != end; entries.increment(error)) {
    if (entries->path().filename().string().starts_with("renderD")) {
      const auto node = validated_character_node(entries->path());
      if (node) candidates.push_back(*node);
    }
  }
  return candidates.size() == 1
      ? std::optional<std::filesystem::path>(candidates.front())
      : std::nullopt;
}

} // namespace

namespace glasswyrm::compositor {

std::unique_ptr<gw::render::SceneRenderer>
create_runtime_renderer(const Options& options, std::string& error) {
  std::optional<std::filesystem::path> report_path;
  if (options.renderer_report) report_path = *options.renderer_report;
  std::unique_ptr<gw::render::SceneRenderer> renderer;
  const gw::render::RendererCreateOptions create_options{
      options.renderer, report_path, validated_render_node(options),
      gw::render::kMaximumGlTextureCacheBytes};
  if (!gw::render::create_scene_renderer(create_options, renderer, error))
    return {};
  return renderer;
}

} // namespace glasswyrm::compositor
