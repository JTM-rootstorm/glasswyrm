#pragma once

#include "render/scene_renderer.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gw::render {

[[nodiscard]] bool create_scene_renderer(
    RendererRequest requested,
    const std::optional<std::filesystem::path>& report_path,
    std::unique_ptr<SceneRenderer>& renderer, std::string& error);

} // namespace gw::render
