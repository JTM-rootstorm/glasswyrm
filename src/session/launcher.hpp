#pragma once

#include "session/process_supervisor.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::session {

struct Options {
  std::string runtime_dir;
  std::uint16_t display{};
  std::string drm_device;
  std::string tty;
  std::string connector;
  std::string mode;
  std::vector<std::string> input_devices;
  std::string xkb_layout = "us";
  std::string xkb_model = "pc105";
  std::string xkb_variant;
  std::string xkb_options;
  std::string drm_api = "auto";
  std::optional<std::string> mirror_dump_dir;
  std::optional<std::string> scene_manifest;
  std::optional<std::string> drm_report;
  std::optional<std::string> x11_trace;
  std::vector<std::string> client;
};

enum class ParseOptionsResult { Run, ExitSuccess, ExitFailure };

struct RuntimePaths {
  std::string wm_socket;
  std::string compositor_socket;
  std::string x11_socket;
};

struct CommandPlan {
  RuntimePaths paths;
  std::vector<ChildSpec> children;
};

ParseOptionsResult parse_options(int argc, char **argv, Options &options,
                                 std::ostream &output, std::ostream &error);

[[nodiscard]] bool make_runtime_paths(const Options &options,
                                      RuntimePaths &paths, std::string &error);
[[nodiscard]] CommandPlan build_command_plan(const Options &options,
                                             const RuntimePaths &paths);
[[nodiscard]] int run_launcher(const Options &options, std::ostream &error,
                               volatile std::sig_atomic_t *pending_signal);

} // namespace glasswyrm::session
