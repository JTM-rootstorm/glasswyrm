#include "gwcomp/options.hpp"
#include "gwcomp/renderer_runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
      options.max_frames != 12 || !options.headless_outputs.empty() ||
      !options.headless_vrr.empty() || options.vrr_report ||
      !output.empty() || !error.empty())
    return 1;

  options = {};
  const std::vector expected_headless_vrr{
      glasswyrm::headless::VrrSimulationRequest{"LEFT", 40'000, 60'000},
      glasswyrm::headless::VrrSimulationRequest{"RIGHT", 48'000, 75'000}};
  const auto vrr_report_path =
      "/tmp/glasswyrm-vrr-options-" +
      std::to_string(static_cast<long long>(::getpid())) + ".jsonl";
  std::filesystem::remove(vrr_report_path);
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "LEFT:800x600@60000",
             "--headless-output", "RIGHT:640x480@75000", "--headless-vrr",
             "LEFT=40000-60000", "--headless-vrr", "RIGHT=48000-75000",
             "--vrr-report", vrr_report_path},
            options, output, error) != ParseOptionsResult::Run ||
      options.headless_vrr != expected_headless_vrr ||
      options.vrr_report != vrr_report_path ||
      !output.empty() || !error.empty())
    return 1;
  std::ofstream(vrr_report_path, std::ios::binary) << "occupied";
  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--vrr-report", vrr_report_path},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("must not already exist") == std::string::npos)
    return 1;
  std::filesystem::remove(vrr_report_path);

  for (const auto *invalid : {"", "LEFT", "=40000-60000",
                              "-LEFT=40000-60000", "LEFT=0-60000",
                              "LEFT=60000-60000", "LEFT=60001-60000",
                              "LEFT=40000", "LEFT=40000-60000-70000"}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--headless-output", "LEFT", "--headless-vrr",
               invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "LEFT", "--headless-vrr",
             "MISSING=40000-60000"},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("unknown") == std::string::npos)
    return 1;
  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "LEFT:800x600@60000",
             "--headless-vrr", "LEFT=40000-60001"},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("nominal") == std::string::npos)
    return 1;
  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "LEFT", "--headless-vrr",
             "LEFT=40000-60000", "--headless-vrr", "LEFT=48000-60000"},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("unique") == std::string::npos)
    return 1;

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "HEADLESS-LEFT",
             "--headless-output", "AUX_2:800x600",
             "--headless-output", "Game.3:1920x1080@59940"},
            options, output, error) != ParseOptionsResult::Run ||
      options.headless_outputs.size() != 3 ||
      options.headless_outputs[0] !=
          glasswyrm::headless::OutputRequest{"HEADLESS-LEFT", 1024, 768,
                                             60'000} ||
      options.headless_outputs[1] !=
          glasswyrm::headless::OutputRequest{"AUX_2", 800, 600, 60'000} ||
      options.headless_outputs[2] !=
          glasswyrm::headless::OutputRequest{"Game.3", 1920, 1080, 59'940} ||
      !output.empty() || !error.empty())
    return 1;

  for (const auto* invalid :
       {"", ":800x600", "-LEFT", "_LEFT", ".LEFT", "LEFT:",
        "LEFT:800", "LEFT:x600", "LEFT:800x", "LEFT:0x600",
        "LEFT:800x0", "LEFT:4097x1", "LEFT:4096x4096@",
        "LEFT:4096x4096@0", "LEFT:1x1@4294967296", "LEFT:800x600:60",
        "LEFT SCREEN", "L/SCREEN", "L\xC3\x89" "FT"}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--headless-output", invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", std::string(64, 'A')},
            options, output, error) != ParseOptionsResult::ExitFailure)
    return 1;

  options = {};
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--headless-output", "LEFT",
             "--headless-output", "LEFT:800x600"},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("names must be unique") == std::string::npos)
    return 1;

  options = {};
  std::vector<std::string> too_many{"gwcomp", "--ipc-socket", "/run/gw.sock",
                                    "--dump-dir", "/tmp/frames"};
  for (int index = 1; index <= 9; ++index) {
    too_many.push_back("--headless-output");
    too_many.push_back("HEADLESS-" + std::to_string(index));
  }
  if (parse(std::move(too_many), options, output, error) !=
          ParseOptionsResult::ExitFailure ||
      error.find("at most 8") == std::string::npos)
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
      output.find("--headless-output") == std::string::npos ||
      output.find("--headless-vrr") == std::string::npos ||
      output.find("--vrr-report") == std::string::npos ||
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
  const auto same_report_path = vrr_report_path + ".same";
  std::filesystem::remove(same_report_path);
  if (parse({"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
             "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
             "--drm-report", same_report_path, "--vrr-report",
             same_report_path},
            options, output, error) != ParseOptionsResult::ExitFailure ||
      error.find("distinct") == std::string::npos)
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
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
       "--headless-output", "HEADLESS-1"},
      {"gwcomp", "--backend", "drm", "--ipc-socket", "/run/gw.sock",
       "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
       "--headless-vrr", "HEADLESS-1=40000-60000"},
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

  std::string temporary = "/tmp/glasswyrm-runtime-renderer-test-XXXXXX";
  if (::mkdtemp(temporary.data()) == nullptr) return 1;
  const auto report_path =
      std::filesystem::path(temporary) / "renderer.jsonl";
  options = {};
  options.renderer_report = report_path.string();
  std::unique_ptr<gw::render::SceneRenderer> renderer;
  std::unique_ptr<gw::render::OutputSceneRenderer> output_renderer;
  if (!glasswyrm::compositor::create_runtime_renderers(
          options, renderer, output_renderer, error))
    return 1;
  std::ifstream report_input(report_path, std::ios::binary);
  const std::string report{std::istreambuf_iterator<char>(report_input), {}};
  const auto first_selection = report.find("{\"record\":\"selection\"");
  if (!renderer || !output_renderer || first_selection == std::string::npos ||
      report.find("{\"record\":\"selection\"", first_selection + 1) !=
          std::string::npos)
    return 1;
  std::filesystem::remove_all(temporary);

  return 0;
}
