#pragma once

#include "backends/headless/inventory.hpp"
#include "backends/headless/vrr_simulation.hpp"
#include "render/scene_renderer.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::compositor {

enum class Backend { Headless, Drm };
enum class DrmApiMode { Auto, Atomic, Legacy };

struct RequestedMode {
  std::uint32_t width{};
  std::uint32_t height{};
  std::optional<std::uint32_t> refresh_millihz;
};

struct Options {
  Backend backend{Backend::Headless};
  std::string ipc_socket;
  std::string dump_dir;
  std::vector<headless::OutputRequest> headless_outputs;
  std::vector<headless::VrrSimulationRequest> headless_vrr;
  std::optional<std::string> drm_device;
  std::optional<int> drm_fd;
  bool external_session{false};
  std::optional<std::string> tty;
  std::optional<std::string> connector;
  std::optional<RequestedMode> mode;
  DrmApiMode drm_api{DrmApiMode::Auto};
  std::optional<std::string> mirror_dump_dir;
  std::optional<std::string> drm_report;
  std::optional<std::string> vrr_report;
  std::optional<std::string> scene_manifest;
  gw::render::RendererRequest renderer{gw::render::RendererRequest::Software};
  std::optional<std::string> renderer_report;
  bool once{false};
  std::optional<std::uint64_t> max_frames;
};

enum class ParseOptionsResult { Run, ExitSuccess, ExitFailure };

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output, std::ostream& error);

}  // namespace glasswyrm::compositor
