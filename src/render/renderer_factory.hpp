#pragma once

#include "render/scene_renderer.hpp"
#include "render/output_scene_renderer.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gw::render {

class RendererReport;

struct RendererCreateOptions {
  RendererRequest requested{RendererRequest::Software};
  std::optional<std::filesystem::path> report_path;
  std::optional<std::filesystem::path> render_node;
  std::uint64_t maximum_texture_bytes{kMaximumGlTextureCacheBytes};
  std::shared_ptr<RendererReport> shared_report;
};

[[nodiscard]] bool create_scene_renderer(
    const RendererCreateOptions& options,
    std::unique_ptr<SceneRenderer>& renderer, std::string& error);

[[nodiscard]] bool create_output_scene_renderer(
    const RendererCreateOptions& options,
    std::unique_ptr<OutputSceneRenderer>& renderer, std::string& error);

[[nodiscard]] bool create_scene_renderer(
    RendererRequest requested,
    const std::optional<std::filesystem::path>& report_path,
    std::unique_ptr<SceneRenderer>& renderer, std::string& error);

} // namespace gw::render
