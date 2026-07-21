#include "gwcomp/options.hpp"

#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <limits>
#include <ostream>
#include <string_view>
#include <utility>

namespace glasswyrm::compositor {
namespace {

void print_usage(std::ostream& output) {
  output <<
      "Usage: gwcomp [--backend headless|drm] --ipc-socket PATH\n"
      "  headless: --dump-dir PATH [--headless-output "
      "NAME[:WIDTHxHEIGHT[@MILLIHZ]]]...\n"
      "            [--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ]...\n"
      "            [--scene-manifest PATH]\n"
      "  drm direct: --drm-device PATH|auto --tty /dev/ttyN\n"
      "  drm external: --drm-fd N --external-session\n"
      "  drm options: [--connector NAME] [--mode WIDTHxHEIGHT[@MILLIHZ]]\n"
      "               [--drm-api auto|atomic|legacy]\n"
      "               [--mirror-dump-dir PATH]\n"
      "               [--mirror-dump-trigger PATH] [--drm-report PATH]\n"
      "  renderer: [--renderer software|gles|auto] [--renderer-report PATH]\n"
      "  common: [--vrr-report PATH] [--once] [--max-frames N]\n"
      "          [--help] [--version]\n";
}

bool parse_positive(std::string_view text, std::uint64_t& value) {
  if (text.empty()) return false;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

bool take_path(int argc, char** argv, int& index, std::string& destination,
               std::string_view option, std::ostream& error) {
  if (++index >= argc || argv[index][0] == '\0') {
    error << "gwcomp: " << option << " requires a non-empty path\n";
    return false;
  }
  destination = argv[index];
  return true;
}

bool parse_nonnegative_fd(std::string_view text, int& value) {
  if (text.empty()) return false;
  unsigned parsed = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc{} || end != text.data() + text.size() ||
      parsed > static_cast<unsigned>(std::numeric_limits<int>::max()))
    return false;
  value = static_cast<int>(parsed);
  return true;
}

bool parse_dimension(std::string_view text, std::uint32_t& value) {
  if (text.empty()) return false;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

bool parse_mode(std::string_view text, RequestedMode& mode) {
  const auto separator = text.find('x');
  if (separator == std::string_view::npos || separator == 0) return false;
  const auto refresh_separator = text.find('@', separator + 1);
  const auto height_end = refresh_separator == std::string_view::npos
                              ? text.size()
                              : refresh_separator;
  if (!parse_dimension(text.substr(0, separator), mode.width) ||
      !parse_dimension(text.substr(separator + 1,
                                   height_end - separator - 1),
                       mode.height))
    return false;
  if (refresh_separator == std::string_view::npos) return true;
  std::uint32_t refresh = 0;
  if (!parse_dimension(text.substr(refresh_separator + 1), refresh))
    return false;
  mode.refresh_millihz = refresh;
  return true;
}

bool parse_headless_output(std::string_view text,
                           headless::OutputRequest& request) {
  const auto separator = text.find(':');
  const auto name_end =
      separator == std::string_view::npos ? text.size() : separator;
  const auto name = text.substr(0, name_end);
  if (!headless::valid_output_name(name))
    return false;
  request.name = std::string(name);
  if (separator == std::string_view::npos)
    return true;

  RequestedMode mode;
  if (!parse_mode(text.substr(separator + 1), mode))
    return false;
  const auto pixels = static_cast<std::uint64_t>(mode.width) * mode.height;
  if (mode.width > output::kMaximumPhysicalExtent ||
      mode.height > output::kMaximumPhysicalExtent ||
      pixels > output::kMaximumPhysicalPixels)
    return false;
  request.width = mode.width;
  request.height = mode.height;
  request.refresh_millihertz =
      mode.refresh_millihz.value_or(headless::kDefaultOutputRefreshMillihertz);
  return true;
}

bool parse_headless_vrr(std::string_view text,
                        headless::VrrSimulationRequest &request) {
  const auto equals = text.find('=');
  if (equals == std::string_view::npos || equals == 0 ||
      !headless::valid_output_name(text.substr(0, equals)))
    return false;
  const auto range = text.substr(equals + 1);
  const auto dash = range.find('-');
  if (dash == std::string_view::npos || dash == 0 ||
      range.find('-', dash + 1) != std::string_view::npos)
    return false;
  std::uint32_t minimum = 0;
  std::uint32_t maximum = 0;
  if (!parse_dimension(range.substr(0, dash), minimum) ||
      !parse_dimension(range.substr(dash + 1), maximum) || minimum >= maximum)
    return false;
  request = {std::string(text.substr(0, equals)), minimum, maximum};
  return true;
}

bool take_optional_path(int argc, char** argv, int& index,
                        std::optional<std::string>& destination,
                        std::string_view option, std::ostream& error) {
  std::string path;
  if (!take_path(argc, argv, index, path, option, error)) return false;
  destination = std::move(path);
  return true;
}

bool validate_backend_options(const Options& options, std::ostream& error) {
  if (options.vrr_report) {
    std::error_code path_error;
    const auto exists = std::filesystem::exists(*options.vrr_report, path_error);
    if (path_error) {
      error << "gwcomp: --vrr-report path cannot be inspected\n";
      return false;
    }
    if (exists) {
      error << "gwcomp: --vrr-report path must not already exist\n";
      return false;
    }
  }
  if (options.drm_report && options.vrr_report) {
    std::error_code drm_path_error;
    std::error_code vrr_path_error;
    const auto drm_path =
        std::filesystem::absolute(*options.drm_report, drm_path_error)
            .lexically_normal();
    const auto vrr_path =
        std::filesystem::absolute(*options.vrr_report, vrr_path_error)
            .lexically_normal();
    if (drm_path_error || vrr_path_error) {
      error << "gwcomp: report paths cannot be resolved\n";
      return false;
    }
    if (drm_path == vrr_path) {
      error << "gwcomp: --drm-report and --vrr-report require distinct paths\n";
      return false;
    }
  }
  if (options.backend == Backend::Headless && options.ipc_socket.empty() &&
      options.dump_dir.empty()) {
    error << "gwcomp: --ipc-socket and --dump-dir are required\n";
    return false;
  }
  if (options.ipc_socket.empty()) {
    error << "gwcomp: --ipc-socket is required\n";
    return false;
  }
  if (options.backend == Backend::Headless) {
    if (options.dump_dir.empty()) {
      error << "gwcomp: --dump-dir is required for the headless backend\n";
      return false;
    }
    if (options.drm_device || options.drm_fd || options.external_session ||
        options.tty || options.connector || options.mode ||
        options.drm_api != DrmApiMode::Auto || options.mirror_dump_dir ||
        options.mirror_dump_trigger || options.drm_report) {
      error << "gwcomp: DRM options require --backend drm\n";
      return false;
    }
    for (const auto &vrr : options.headless_vrr) {
      const auto configured = std::ranges::find_if(
          options.headless_outputs, [&vrr](const auto &output) {
            return output.name == vrr.name;
          });
      const auto historical = options.headless_outputs.empty() &&
                              vrr.name == headless::kDefaultOutputName;
      if (configured == options.headless_outputs.end() && !historical) {
        error << "gwcomp: --headless-vrr names an unknown headless output\n";
        return false;
      }
      const auto nominal = historical
                               ? headless::kDefaultOutputRefreshMillihertz
                               : configured->refresh_millihertz;
      if (vrr.maximum_refresh_millihertz > nominal) {
        error << "gwcomp: --headless-vrr maximum must not exceed nominal "
                 "output refresh\n";
        return false;
      }
    }
    return true;
  }
  if (!options.headless_outputs.empty() || !options.headless_vrr.empty()) {
    error << "gwcomp: --headless-output and --headless-vrr require --backend "
             "headless\n";
    return false;
  }
  if (!options.dump_dir.empty()) {
    error << "gwcomp: --dump-dir is headless-only; use --mirror-dump-dir for DRM\n";
    return false;
  }
  if (options.mirror_dump_trigger && !options.mirror_dump_dir) {
    error << "gwcomp: --mirror-dump-trigger requires --mirror-dump-dir\n";
    return false;
  }
  if (options.drm_device && options.drm_fd) {
    error << "gwcomp: --drm-device and --drm-fd are mutually exclusive\n";
    return false;
  }
  if (!options.drm_device && !options.drm_fd) {
    error << "gwcomp: DRM requires --drm-device or --drm-fd\n";
    return false;
  }
  if (options.drm_fd) {
    if (!options.external_session || options.tty) {
      error << "gwcomp: --drm-fd requires --external-session and forbids --tty\n";
      return false;
    }
  } else if (options.external_session || !options.tty) {
    error << "gwcomp: direct DRM requires --tty and forbids --external-session\n";
    return false;
  }
  return true;
}

}  // namespace

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output, std::ostream& error) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_usage(output);
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--version") {
      output << "gwcomp " << GW_PROJECT_VERSION << '\n';
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--backend") {
      std::string value;
      if (!take_path(argc, argv, index, value, argument, error))
        return ParseOptionsResult::ExitFailure;
      if (value == "headless") options.backend = Backend::Headless;
      else if (value == "drm") options.backend = Backend::Drm;
      else {
        error << "gwcomp: --backend requires headless or drm\n";
        return ParseOptionsResult::ExitFailure;
      }
      continue;
    }
    if (argument == "--ipc-socket") {
      if (!take_path(argc, argv, index, options.ipc_socket, argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--dump-dir") {
      if (!take_path(argc, argv, index, options.dump_dir, argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--headless-output") {
      std::string value;
      if (!take_path(argc, argv, index, value, argument, error))
        return ParseOptionsResult::ExitFailure;
      headless::OutputRequest request;
      if (!parse_headless_output(value, request)) {
        error << "gwcomp: --headless-output requires "
                 "NAME[:WIDTHxHEIGHT[@MILLIHZ]]; NAME is a 1-63 byte ASCII "
                 "identifier and dimensions are bounded to 4096x4096\n";
        return ParseOptionsResult::ExitFailure;
      }
      if (options.headless_outputs.size() >= output::kMaximumOutputs) {
        error << "gwcomp: --headless-output may be specified at most 8 times\n";
        return ParseOptionsResult::ExitFailure;
      }
      if (std::ranges::any_of(options.headless_outputs,
                              [&request](const auto& existing) {
                                return existing.name == request.name;
                              })) {
        error << "gwcomp: --headless-output names must be unique\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.headless_outputs.push_back(std::move(request));
      continue;
    }
    if (argument == "--headless-vrr") {
      std::string value;
      if (!take_path(argc, argv, index, value, argument, error))
        return ParseOptionsResult::ExitFailure;
      headless::VrrSimulationRequest request;
      if (!parse_headless_vrr(value, request)) {
        error << "gwcomp: --headless-vrr requires "
                 "NAME=MIN-MILLIHZ-MAX-MILLIHZ with 0 < MIN < MAX\n";
        return ParseOptionsResult::ExitFailure;
      }
      if (std::ranges::any_of(options.headless_vrr,
                              [&request](const auto &existing) {
                                return existing.name == request.name;
                              })) {
        error << "gwcomp: --headless-vrr names must be unique\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.headless_vrr.push_back(std::move(request));
      continue;
    }
    if (argument == "--drm-device") {
      if (!take_optional_path(argc, argv, index, options.drm_device, argument,
                              error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--drm-fd") {
      int fd = -1;
      if (++index >= argc || !parse_nonnegative_fd(argv[index], fd)) {
        error << "gwcomp: --drm-fd requires a non-negative integer\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.drm_fd = fd;
      continue;
    }
    if (argument == "--external-session") {
      options.external_session = true;
      continue;
    }
    if (argument == "--tty") {
      if (!take_optional_path(argc, argv, index, options.tty, argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--connector") {
      if (!take_optional_path(argc, argv, index, options.connector, argument,
                              error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--mode") {
      RequestedMode mode;
      if (++index >= argc || !parse_mode(argv[index], mode)) {
        error << "gwcomp: --mode requires WIDTHxHEIGHT[@MILLIHZ]\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.mode = mode;
      continue;
    }
    if (argument == "--drm-api") {
      std::string value;
      if (!take_path(argc, argv, index, value, argument, error))
        return ParseOptionsResult::ExitFailure;
      if (value == "auto") options.drm_api = DrmApiMode::Auto;
      else if (value == "atomic") options.drm_api = DrmApiMode::Atomic;
      else if (value == "legacy") options.drm_api = DrmApiMode::Legacy;
      else {
        error << "gwcomp: --drm-api requires auto, atomic, or legacy\n";
        return ParseOptionsResult::ExitFailure;
      }
      continue;
    }
    if (argument == "--mirror-dump-dir") {
      if (!take_optional_path(argc, argv, index, options.mirror_dump_dir,
                              argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--mirror-dump-trigger") {
      if (!take_optional_path(argc, argv, index, options.mirror_dump_trigger,
                              argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--drm-report") {
      if (!take_optional_path(argc, argv, index, options.drm_report, argument,
                              error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--vrr-report") {
      if (!take_optional_path(argc, argv, index, options.vrr_report, argument,
                              error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--scene-manifest") {
      std::string path;
      if (!take_path(argc, argv, index, path, argument, error))
        return ParseOptionsResult::ExitFailure;
      options.scene_manifest = path;
      continue;
    }
    if (argument == "--renderer") {
      std::string value;
      if (!take_path(argc, argv, index, value, argument, error))
        return ParseOptionsResult::ExitFailure;
      if (value == "software")
        options.renderer = gw::render::RendererRequest::Software;
      else if (value == "gles")
        options.renderer = gw::render::RendererRequest::Gles;
      else if (value == "auto")
        options.renderer = gw::render::RendererRequest::Auto;
      else {
        error << "gwcomp: --renderer requires software, gles, or auto\n";
        return ParseOptionsResult::ExitFailure;
      }
      continue;
    }
    if (argument == "--renderer-report") {
      if (!take_optional_path(argc, argv, index, options.renderer_report,
                              argument, error))
        return ParseOptionsResult::ExitFailure;
      continue;
    }
    if (argument == "--once") {
      options.once = true;
      continue;
    }
    if (argument == "--max-frames") {
      std::uint64_t count = 0;
      if (++index >= argc || !parse_positive(argv[index], count)) {
        error << "gwcomp: --max-frames requires a positive integer\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.max_frames = count;
      continue;
    }
    error << "gwcomp: unknown option: " << argument << '\n';
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }

  if (!validate_backend_options(options, error)) {
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  return ParseOptionsResult::Run;
}

}  // namespace glasswyrm::compositor
