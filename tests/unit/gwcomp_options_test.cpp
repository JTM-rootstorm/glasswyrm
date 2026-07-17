#include "gwcomp/options.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

using glasswyrm::compositor::Options;
using glasswyrm::compositor::ParseOptionsResult;

ParseOptionsResult parse(std::vector<std::string> arguments, Options& options,
                         std::string& output, std::string& error) {
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::compositor::parse_options(
      static_cast<int>(argv.size()), argv.data(), options, output_stream,
      error_stream);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

}  // namespace

int main() {
  Options options;
  std::string output;
  std::string error;
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--once", "--max-frames", "12"},
            options, output, error) != ParseOptionsResult::Run ||
      options.ipc_socket != "/run/gw.sock" ||
      options.dump_dir != "/tmp/frames" || !options.once ||
      options.max_frames != 12 || !output.empty() || !error.empty())
    return 1;

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--scene-manifest", "/tmp/scenes.jsonl"},
            options, output, error) != ParseOptionsResult::Run ||
      options.scene_manifest != "/tmp/scenes.jsonl" ||
      options.renderer != gw::render::RendererRequest::Software ||
      options.renderer_report)
    return 1;

  for (const auto& renderer : {std::string("software"), std::string("gles"),
                               std::string("auto")}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--renderer", renderer, "--renderer-report",
               "/tmp/renderer.jsonl"},
              options, output, error) != ParseOptionsResult::Run ||
        std::string(gw::render::renderer_request_name(options.renderer)) !=
            renderer ||
        options.renderer_report != "/tmp/renderer.jsonl")
      return 1;
  }

  for (const auto* invalid : {"", "gl", "Software"}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--renderer", invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--renderer-report", ""},
            options, output, error) != ParseOptionsResult::ExitFailure)
    return 1;

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--scene-manifest", ""},
            options, output, error) != ParseOptionsResult::ExitFailure)
    return 1;

  options = {};
  if (parse({"gwcomp", "--help"}, options, output, error) !=
          ParseOptionsResult::ExitSuccess ||
      output.find("Usage: gwcomp") == std::string::npos ||
      output.find("--renderer software|gles|auto") == std::string::npos ||
      output.find("--renderer-report PATH") == std::string::npos)
    return 1;

  options = {};
  if (parse({"gwcomp"}, options, output, error) !=
          ParseOptionsResult::ExitFailure ||
      error.find("are required") == std::string::npos)
    return 1;

  for (const auto* invalid : {"0", "-1", "abc", "12x"}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--max-frames", invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  options = {};
  if (parse({"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
             "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
             "--connector", "Virtual-1", "--mode", "1024x768@60000",
             "--drm-api", "atomic", "--mirror-dump-dir", "/tmp/mirror",
             "--drm-report", "/tmp/report.jsonl"},
            options, output, error) != ParseOptionsResult::Run ||
      options.backend != glasswyrm::compositor::Backend::Drm ||
      options.drm_device != "/dev/dri/card0" || options.tty != "/dev/tty2" ||
      options.connector != "Virtual-1" || !options.mode ||
      options.mode->width != 1024 || options.mode->height != 768 ||
      options.mode->refresh_millihz != 60000 ||
      options.drm_api != glasswyrm::compositor::DrmApiMode::Atomic ||
      options.mirror_dump_dir != "/tmp/mirror" ||
      options.drm_report != "/tmp/report.jsonl")
    return 1;

  options = {};
  if (parse({"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
             "--drm-fd", "7", "--external-session", "--mode", "800x600"},
            options, output, error) != ParseOptionsResult::Run ||
      options.drm_fd != 7 || !options.external_session || !options.mode ||
      options.mode->refresh_millihz)
    return 1;

  const std::vector<std::vector<std::string>> invalid_drm = {
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-device", "/dev/dri/card0"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-fd", "7"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-fd", "7", "--external-session", "--tty", "/dev/tty2"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-device", "/dev/dri/card0", "--drm-fd", "7",
       "--external-session"},
      {"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
       "/tmp/frames", "--connector", "Virtual-1"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
       "--dump-dir", "/tmp/frames"},
  };
  for (auto arguments : invalid_drm) {
    options = {};
    if (parse(std::move(arguments), options, output, error) !=
        ParseOptionsResult::ExitFailure)
      return 1;
  }

  for (const auto* invalid : {"1024", "x768", "1024x", "0x768",
                              "1024x0", "1024x768@", "1024x768@0",
                              "1024x768@60x"}) {
    options = {};
    if (parse({"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
               "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
               "--mode", invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  return 0;
}
