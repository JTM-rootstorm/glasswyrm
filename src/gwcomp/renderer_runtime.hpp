#pragma once

#include "gwcomp/options.hpp"
#include "render/scene_renderer.hpp"
#include "render/output_scene_renderer.hpp"
#include "render/renderer_report.hpp"

#include <memory>
#include <string>

namespace glasswyrm::compositor {

[[nodiscard]] bool create_runtime_renderers(
    const Options& options, std::unique_ptr<gw::render::SceneRenderer>& renderer,
    std::unique_ptr<gw::render::OutputSceneRenderer>& output_renderer,
    std::string& error);

[[nodiscard]] std::unique_ptr<gw::render::SceneRenderer>
create_runtime_renderer(const Options& options, std::string& error);

[[nodiscard]] std::unique_ptr<gw::render::SceneRenderer>
create_runtime_renderer(const Options& options,
                        std::shared_ptr<gw::render::RendererReport> report,
                        std::string& error);

[[nodiscard]] std::unique_ptr<gw::render::OutputSceneRenderer>
create_runtime_output_renderer(const Options& options, std::string& error);

[[nodiscard]] std::unique_ptr<gw::render::OutputSceneRenderer>
create_runtime_output_renderer(
    const Options& options, std::shared_ptr<gw::render::RendererReport> report,
    std::string& error);

} // namespace glasswyrm::compositor
