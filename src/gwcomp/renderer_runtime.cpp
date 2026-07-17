#include "gwcomp/renderer_runtime.hpp"

#include "render/renderer_factory.hpp"

#include <filesystem>
#include <optional>

namespace glasswyrm::compositor {

std::unique_ptr<gw::render::SceneRenderer>
create_runtime_renderer(const Options& options, std::string& error) {
  std::optional<std::filesystem::path> report_path;
  if (options.renderer_report) report_path = *options.renderer_report;
  std::unique_ptr<gw::render::SceneRenderer> renderer;
  if (!gw::render::create_scene_renderer(options.renderer, report_path,
                                         renderer, error))
    return {};
  return renderer;
}

} // namespace glasswyrm::compositor
