#include "session/launcher.hpp"

#include "config.hpp"

#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <ostream>
#include <string_view>

namespace glasswyrm::session {
namespace {

struct Seen {
  bool runtime_dir{};
  bool display{};
  bool drm_device{};
  bool tty{};
  bool connector{};
  bool mode{};
  bool xkb_layout{};
  bool xkb_model{};
  bool xkb_variant{};
  bool xkb_options{};
  bool drm_api{};
  bool mirror_dump_dir{};
  bool scene_manifest{};
  bool drm_report{};
  bool x11_trace{};
};

void print_usage(std::ostream &output) {
  output << "Usage: glasswyrm-session --runtime-dir PATH --display N\n"
            "  --drm-device PATH --tty PATH --connector NAME\n"
            "  --mode WIDTHxHEIGHT[@MILLIHZ] --input-device PATH ...\n"
            "  [--xkb-layout NAME] [--xkb-model NAME] [--xkb-variant NAME]\n"
            "  [--xkb-options LIST] [--drm-api auto|atomic|legacy]\n"
            "  [--mirror-dump-dir PATH] [--scene-manifest PATH]\n"
            "  [--drm-report PATH] [--x11-trace PATH]\n"
            "  [--client PROGRAM ARG...] [--help] [--version]\n";
}

bool parse_display(std::string_view text, std::uint16_t &display) {
  unsigned value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size() || value > 65535)
    return false;
  display = static_cast<std::uint16_t>(value);
  return true;
}

bool take_value(int argc, char **argv, int &index, std::string_view option,
                std::string &value, bool &seen, std::ostream &error,
                bool permit_empty = false) {
  if (seen) {
    error << "glasswyrm-session: duplicate option: " << option << '\n';
    return false;
  }
  if (++index >= argc || (!permit_empty && argv[index][0] == '\0')) {
    error << "glasswyrm-session: " << option << " requires "
          << (permit_empty ? "a value" : "a non-empty value") << '\n';
    return false;
  }
  seen = true;
  value = argv[index];
  return true;
}

bool take_optional(int argc, char **argv, int &index, std::string_view option,
                   std::optional<std::string> &value, bool &seen,
                   std::ostream &error) {
  std::string parsed;
  if (!take_value(argc, argv, index, option, parsed, seen, error))
    return false;
  value = std::move(parsed);
  return true;
}

bool validate(const Options &options, const Seen &seen, std::ostream &error) {
  if (!seen.runtime_dir || !seen.display || !seen.drm_device || !seen.tty ||
      !seen.connector || !seen.mode || options.input_devices.empty()) {
    error << "glasswyrm-session: runtime-dir, display, drm-device, tty, "
             "connector, mode, and at least one input-device are required\n";
    print_usage(error);
    return false;
  }
  if (options.runtime_dir.front() != '/') {
    error << "glasswyrm-session: --runtime-dir must be an absolute path\n";
    return false;
  }
  if (options.drm_api != "auto" && options.drm_api != "atomic" &&
      options.drm_api != "legacy") {
    error << "glasswyrm-session: --drm-api requires auto, atomic, or legacy\n";
    return false;
  }
  if (std::any_of(options.input_devices.begin(), options.input_devices.end(),
                  [](const auto &path) { return path.empty(); })) {
    error << "glasswyrm-session: --input-device requires a non-empty path\n";
    return false;
  }
  return true;
}

void append_option(std::vector<std::string> &argv, std::string_view option,
                   const std::optional<std::string> &value) {
  if (!value)
    return;
  argv.emplace_back(option);
  argv.push_back(*value);
}

bool secure_runtime_directory(const std::string &path, bool &created,
                              std::string &error) {
  created = false;
  if (::mkdir(path.c_str(), 0700) == 0) {
    created = true;
    return true;
  }
  if (errno != EEXIST) {
    error = "cannot create runtime directory '" + path +
            "': " + std::strerror(errno);
    return false;
  }
  struct stat status{};
  if (::lstat(path.c_str(), &status) < 0 || !S_ISDIR(status.st_mode) ||
      S_ISLNK(status.st_mode) || status.st_uid != ::geteuid() ||
      (status.st_mode & 0777) != 0700) {
    error = "existing runtime path must be an owned mode-0700 directory";
    return false;
  }
  return true;
}

} // namespace

ParseOptionsResult parse_options(int argc, char **argv, Options &options,
                                 std::ostream &output, std::ostream &error) {
  Seen seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_usage(output);
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--version") {
      output << "glasswyrm-session " << GW_PROJECT_VERSION << '\n';
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--client") {
      if (++index >= argc || argv[index][0] == '\0') {
        error << "glasswyrm-session: --client requires a program\n";
        return ParseOptionsResult::ExitFailure;
      }
      for (; index < argc; ++index)
        options.client.emplace_back(argv[index]);
      break;
    }
    bool parsed = true;
    if (argument == "--runtime-dir")
      parsed = take_value(argc, argv, index, argument, options.runtime_dir,
                          seen.runtime_dir, error);
    else if (argument == "--display") {
      if (seen.display || ++index >= argc ||
          !parse_display(index < argc ? argv[index] : "", options.display)) {
        error << "glasswyrm-session: --display requires an integer from 0 to "
                 "65535\n";
        return ParseOptionsResult::ExitFailure;
      }
      seen.display = true;
    } else if (argument == "--drm-device")
      parsed = take_value(argc, argv, index, argument, options.drm_device,
                          seen.drm_device, error);
    else if (argument == "--tty")
      parsed =
          take_value(argc, argv, index, argument, options.tty, seen.tty, error);
    else if (argument == "--connector")
      parsed = take_value(argc, argv, index, argument, options.connector,
                          seen.connector, error);
    else if (argument == "--mode")
      parsed = take_value(argc, argv, index, argument, options.mode, seen.mode,
                          error);
    else if (argument == "--input-device") {
      if (++index >= argc || argv[index][0] == '\0')
        parsed = false;
      else
        options.input_devices.emplace_back(argv[index]);
      if (!parsed)
        error
            << "glasswyrm-session: --input-device requires a non-empty path\n";
    } else if (argument == "--xkb-layout")
      parsed = take_value(argc, argv, index, argument, options.xkb_layout,
                          seen.xkb_layout, error);
    else if (argument == "--xkb-model")
      parsed = take_value(argc, argv, index, argument, options.xkb_model,
                          seen.xkb_model, error);
    else if (argument == "--xkb-variant")
      parsed = take_value(argc, argv, index, argument, options.xkb_variant,
                          seen.xkb_variant, error, true);
    else if (argument == "--xkb-options")
      parsed = take_value(argc, argv, index, argument, options.xkb_options,
                          seen.xkb_options, error, true);
    else if (argument == "--drm-api")
      parsed = take_value(argc, argv, index, argument, options.drm_api,
                          seen.drm_api, error);
    else if (argument == "--mirror-dump-dir")
      parsed =
          take_optional(argc, argv, index, argument, options.mirror_dump_dir,
                        seen.mirror_dump_dir, error);
    else if (argument == "--scene-manifest")
      parsed =
          take_optional(argc, argv, index, argument, options.scene_manifest,
                        seen.scene_manifest, error);
    else if (argument == "--drm-report")
      parsed = take_optional(argc, argv, index, argument, options.drm_report,
                             seen.drm_report, error);
    else if (argument == "--x11-trace")
      parsed = take_optional(argc, argv, index, argument, options.x11_trace,
                             seen.x11_trace, error);
    else {
      error << "glasswyrm-session: unknown option: " << argument << '\n';
      print_usage(error);
      return ParseOptionsResult::ExitFailure;
    }
    if (!parsed)
      return ParseOptionsResult::ExitFailure;
  }
  return validate(options, seen, error) ? ParseOptionsResult::Run
                                        : ParseOptionsResult::ExitFailure;
}

bool make_runtime_paths(const Options &options, RuntimePaths &paths,
                        std::string &error) {
  paths.wm_socket = options.runtime_dir + "/gwm.sock";
  paths.compositor_socket = options.runtime_dir + "/gwcomp.sock";
  paths.x11_socket = "/tmp/.X11-unix/X" + std::to_string(options.display);
  if (paths.wm_socket.size() >= sizeof(sockaddr_un::sun_path) ||
      paths.compositor_socket.size() >= sizeof(sockaddr_un::sun_path)) {
    error = "runtime directory is too long for Unix socket paths";
    return false;
  }
  error.clear();
  return true;
}

CommandPlan build_command_plan(const Options &options,
                               const RuntimePaths &paths) {
  CommandPlan plan;
  plan.paths = paths;
  plan.children.push_back({"gwm",
                           {"gwm", "--ipc-socket", paths.wm_socket},
                           {},
                           paths.wm_socket,
                           true,
                           true});

  auto &compositor = plan.children.emplace_back();
  compositor.name = "gwcomp";
  compositor.argv = {"gwcomp",
                     "--backend",
                     "drm",
                     "--ipc-socket",
                     paths.compositor_socket,
                     "--drm-device",
                     options.drm_device,
                     "--tty",
                     options.tty,
                     "--connector",
                     options.connector,
                     "--mode",
                     options.mode,
                     "--drm-api",
                     options.drm_api};
  append_option(compositor.argv, "--mirror-dump-dir", options.mirror_dump_dir);
  append_option(compositor.argv, "--scene-manifest", options.scene_manifest);
  append_option(compositor.argv, "--drm-report", options.drm_report);
  compositor.readiness_socket = paths.compositor_socket;

  auto &server = plan.children.emplace_back();
  server.name = "glasswyrmd";
  server.argv = {"glasswyrmd",
                 "--display",
                 std::to_string(options.display),
                 "--wm-socket",
                 paths.wm_socket,
                 "--compositor-socket",
                 paths.compositor_socket,
                 "--software-content",
                 "--xkb-layout",
                 options.xkb_layout,
                 "--xkb-model",
                 options.xkb_model};
  for (const auto &device : options.input_devices) {
    server.argv.emplace_back("--libinput-device");
    server.argv.push_back(device);
  }
  if (!options.xkb_variant.empty()) {
    server.argv.emplace_back("--xkb-variant");
    server.argv.push_back(options.xkb_variant);
  }
  if (!options.xkb_options.empty()) {
    server.argv.emplace_back("--xkb-options");
    server.argv.push_back(options.xkb_options);
  }
  append_option(server.argv, "--x11-trace", options.x11_trace);
  server.readiness_socket = paths.x11_socket;

  if (!options.client.empty()) {
    auto &client = plan.children.emplace_back();
    client.name = "initial client";
    client.argv = options.client;
    client.environment = {"DISPLAY=:" + std::to_string(options.display)};
    client.required = false;
  }
  return plan;
}

int run_launcher(const Options &options, std::ostream &error,
                 volatile std::sig_atomic_t *pending_signal) {
  RuntimePaths paths;
  std::string detail;
  if (!make_runtime_paths(options, paths, detail)) {
    error << "glasswyrm-session: " << detail << '\n';
    return 2;
  }
  bool created = false;
  if (!secure_runtime_directory(options.runtime_dir, created, detail)) {
    error << "glasswyrm-session: " << detail << '\n';
    return 2;
  }
  for (const auto *path : {&paths.wm_socket, &paths.compositor_socket}) {
    struct stat status{};
    if (::lstat(path->c_str(), &status) == 0 || errno != ENOENT) {
      error << "glasswyrm-session: runtime socket path is already occupied: "
            << *path << '\n';
      if (created)
        (void)::rmdir(options.runtime_dir.c_str());
      return 2;
    }
  }

  ProcessSupervisor supervisor;
  const int result = supervisor.run(build_command_plan(options, paths).children,
                                    error, pending_signal);
  (void)::unlink(paths.compositor_socket.c_str());
  (void)::unlink(paths.wm_socket.c_str());
  if (created)
    (void)::rmdir(options.runtime_dir.c_str());
  return result;
}

} // namespace glasswyrm::session
