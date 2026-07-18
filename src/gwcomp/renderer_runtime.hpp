#pragma once

#include "gwcomp/options.hpp"
#include "render/scene_renderer.hpp"
#include "render/output_scene_renderer.hpp"

#include <memory>
#include <string>

namespace glasswyrm::compositor {

[[nodiscard]] std::unique_ptr<gw::render::SceneRenderer>
create_runtime_renderer(const Options& options, std::string& error);

[[nodiscard]] std::unique_ptr<gw::render::OutputSceneRenderer>
create_runtime_output_renderer(const Options& options, std::string& error);

} // namespace glasswyrm::compositor
