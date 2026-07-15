#include "glasswyrmd/options.hpp"

#include "config.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace glasswyrm::server {
namespace {

void print_usage(std::ostream &output) {
  output << "Usage: glasswyrmd [--display N] [--socket-dir PATH] [--help] "
            "[--version] [--wm-socket PATH --compositor-socket PATH] "
            "[--software-content] [--synthetic-input-socket PATH] "
            "[--x11-trace PATH] [--libinput-device PATH]... "
            "[--xkb-rules NAME] [--xkb-model NAME] [--xkb-layout NAME] "
            "[--xkb-variant NAME] [--xkb-options LIST] "
            "[--repeat-delay-ms N] [--repeat-rate-hz N]\n";
}

bool parse_display(std::string_view text, std::uint16_t &display) {
  if (text.empty()) {
    return false;
  }
  unsigned int value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      value > 65535) {
    return false;
  }
  display = static_cast<std::uint16_t>(value);
  return true;
}

bool parse_positive(std::string_view text, std::uint32_t maximum,
                    std::uint32_t &result) {
  if (text.empty())
    return false;
  std::uint64_t value = 0;
  const auto [end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_error != std::errc{} || end != text.data() + text.size() ||
      value == 0 || value > maximum)
    return false;
  result = static_cast<std::uint32_t>(value);
  return true;
}

bool beneath_input_directory(std::string_view path) noexcept {
  constexpr std::string_view prefix = "/dev/input/";
  return path.size() > prefix.size() && path.starts_with(prefix);
}

bool same_identity(const InputDevicePath &expected,
                   const struct stat &status) noexcept {
  return S_ISCHR(status.st_mode) &&
         expected.containing_device ==
             static_cast<std::uint64_t>(status.st_dev) &&
         expected.inode == static_cast<std::uint64_t>(status.st_ino) &&
         expected.special_device == static_cast<std::uint64_t>(status.st_rdev);
}

enum class ArgumentResult {
  NotHandled,
  Parsed,
  ExitFailure,
};

struct SeenOptions {
  bool xkb_rules = false;
  bool xkb_model = false;
  bool xkb_layout = false;
  bool xkb_variant = false;
  bool xkb_options = false;
  bool repeat_delay = false;
  bool repeat_rate = false;
};

ArgumentResult parse_base_argument(std::string_view argument, int &index,
                                   int argc, char **argv, Options &options,
                                   std::ostream &error) {
  if (argument == "--display") {
    if (++index >= argc || !parse_display(argv[index], options.display)) {
      error << "glasswyrmd: --display requires an integer from 0 to 65535\n";
      return ArgumentResult::ExitFailure;
    }
    return ArgumentResult::Parsed;
  }
  if (argument == "--socket-dir") {
    if (++index >= argc || argv[index][0] == '\0') {
      error << "glasswyrmd: --socket-dir requires a non-empty path\n";
      return ArgumentResult::ExitFailure;
    }
    options.socket_dir = argv[index];
    return ArgumentResult::Parsed;
  }
  if (argument == "--software-content") {
    if (options.software_content) {
      error << "glasswyrmd: duplicate option: --software-content\n";
      return ArgumentResult::ExitFailure;
    }
    options.software_content = true;
    return ArgumentResult::Parsed;
  }
  if (argument == "--synthetic-input-socket" || argument == "--x11-trace") {
    if (++index >= argc || argv[index][0] == '\0') {
      error << "glasswyrmd: " << argument << " requires a non-empty path\n";
      return ArgumentResult::ExitFailure;
    }
    auto &destination = argument == "--synthetic-input-socket"
                            ? options.synthetic_input_socket
                            : options.x11_trace;
    if (destination.has_value()) {
      error << "glasswyrmd: duplicate option: " << argument << '\n';
      return ArgumentResult::ExitFailure;
    }
    destination = argv[index];
    return ArgumentResult::Parsed;
  }
  if (argument == "--wm-socket" || argument == "--compositor-socket") {
    if (++index >= argc || argv[index][0] == '\0') {
      error << "glasswyrmd: " << argument << " requires a non-empty path\n";
      return ArgumentResult::ExitFailure;
    }
    auto &destination = argument == "--wm-socket" ? options.wm_socket
                                                  : options.compositor_socket;
    if (destination.has_value()) {
      error << "glasswyrmd: duplicate option: " << argument << '\n';
      return ArgumentResult::ExitFailure;
    }
    destination = argv[index];
    return ArgumentResult::Parsed;
  }
  return ArgumentResult::NotHandled;
}

ArgumentResult parse_device_argument(std::string_view argument, int &index,
                                     int argc, char **argv, Options &options,
                                     std::ostream &error,
                                     InputDevicePathResolver resolve_device) {
  if (argument != "--libinput-device")
    return ArgumentResult::NotHandled;
#if !GW_HAS_LIBINPUT_BACKEND
  error << "glasswyrmd: --libinput-device is unavailable because this build "
           "does not include the libinput backend\n";
  return ArgumentResult::ExitFailure;
#endif
  if (++index >= argc || argv[index][0] == '\0') {
    error << "glasswyrmd: --libinput-device requires a non-empty path\n";
    return ArgumentResult::ExitFailure;
  }
  if (!resolve_device) {
    error << "glasswyrmd: input-device resolver is unavailable\n";
    return ArgumentResult::ExitFailure;
  }
  InputDevicePath resolved;
  std::string detail;
  if (!resolve_device(argv[index], resolved, detail)) {
    error << "glasswyrmd: invalid --libinput-device: " << detail << '\n';
    return ArgumentResult::ExitFailure;
  }
  if (std::any_of(options.libinput_devices.begin(),
                  options.libinput_devices.end(), [&](const auto &prior) {
                    return prior.canonical_path == resolved.canonical_path;
                  })) {
    error << "glasswyrmd: duplicate --libinput-device: "
          << resolved.canonical_path << '\n';
    return ArgumentResult::ExitFailure;
  }
  options.libinput_devices.push_back(std::move(resolved));
  return ArgumentResult::Parsed;
}

ArgumentResult parse_xkb_argument(std::string_view argument, int &index,
                                  int argc, char **argv, Options &options,
                                  SeenOptions &seen, std::ostream &error) {
  bool *was_seen = nullptr;
  std::string *destination = nullptr;
  bool permits_empty = false;
  if (argument == "--xkb-rules") {
    was_seen = &seen.xkb_rules;
    destination = &options.xkb_rules;
  } else if (argument == "--xkb-model") {
    was_seen = &seen.xkb_model;
    destination = &options.xkb_model;
  } else if (argument == "--xkb-layout") {
    was_seen = &seen.xkb_layout;
    destination = &options.xkb_layout;
  } else if (argument == "--xkb-variant") {
    was_seen = &seen.xkb_variant;
    destination = &options.xkb_variant;
    permits_empty = true;
  } else if (argument == "--xkb-options") {
    was_seen = &seen.xkb_options;
    destination = &options.xkb_options;
    permits_empty = true;
  } else {
    return ArgumentResult::NotHandled;
  }
  if (++index >= argc) {
    error << "glasswyrmd: " << argument << " requires a value\n";
    return ArgumentResult::ExitFailure;
  }
  if (*was_seen || (!permits_empty && argv[index][0] == '\0')) {
    error << "glasswyrmd: "
          << (*was_seen ? "duplicate option: " : "non-empty value required: ")
          << argument << '\n';
    return ArgumentResult::ExitFailure;
  }
  *was_seen = true;
  *destination = argv[index];
  return ArgumentResult::Parsed;
}

ArgumentResult parse_repeat_argument(std::string_view argument, int &index,
                                     int argc, char **argv, Options &options,
                                     SeenOptions &seen, std::ostream &error) {
  const bool is_delay = argument == "--repeat-delay-ms";
  if (!is_delay && argument != "--repeat-rate-hz")
    return ArgumentResult::NotHandled;
  bool &was_seen = is_delay ? seen.repeat_delay : seen.repeat_rate;
  auto &destination =
      is_delay ? options.repeat_delay_ms : options.repeat_rate_hz;
  const auto maximum = is_delay ? std::numeric_limits<std::uint32_t>::max()
                                : UINT32_C(1000000000);
  if (was_seen || ++index >= argc ||
      !parse_positive(index < argc ? argv[index] : "", maximum, destination)) {
    error << "glasswyrmd: " << argument << " requires an integer from 1 to "
          << maximum << '\n';
    return ArgumentResult::ExitFailure;
  }
  was_seen = true;
  return ArgumentResult::Parsed;
}

ParseOptionsResult validate_options(const Options &options,
                                    InputDevicePathResolver resolve_device,
                                    std::ostream &error) {
  if (options.wm_socket.has_value() != options.compositor_socket.has_value()) {
    error << "glasswyrmd: --wm-socket and --compositor-socket must be supplied "
             "together\n";
    return ParseOptionsResult::ExitFailure;
  }
  if (options.software_content && !options.integrated()) {
    error << "glasswyrmd: --software-content requires --wm-socket and "
             "--compositor-socket\n";
    return ParseOptionsResult::ExitFailure;
  }
  if (options.synthetic_input_socket && !options.integrated()) {
    error << "glasswyrmd: --synthetic-input-socket requires --wm-socket and "
             "--compositor-socket\n";
    return ParseOptionsResult::ExitFailure;
  }
  if (options.real_input_enabled() && options.synthetic_input_socket) {
    error << "glasswyrmd: --libinput-device and --synthetic-input-socket are "
             "mutually exclusive\n";
    return ParseOptionsResult::ExitFailure;
  }
  if (options.real_input_enabled() && !options.integrated()) {
    error << "glasswyrmd: --libinput-device requires --wm-socket and "
             "--compositor-socket\n";
    return ParseOptionsResult::ExitFailure;
  }
  if (options.real_input_enabled() &&
      resolve_device == resolve_input_device_path) {
    std::string detail;
    if (!validate_input_device_paths(options, detail)) {
      error << "glasswyrmd: " << detail << '\n';
      return ParseOptionsResult::ExitFailure;
    }
  }
  return ParseOptionsResult::Run;
}

} // namespace

bool resolve_input_device_path(std::string_view path, InputDevicePath &resolved,
                               std::string &error) {
  resolved = {};
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    error = "input device path must be non-empty and contain no NUL";
    return false;
  }
  const std::string owned(path);
  char *raw = ::realpath(owned.c_str(), nullptr);
  if (!raw) {
    error = "could not canonicalize input device '" + owned +
            "': " + std::strerror(errno);
    return false;
  }
  std::string canonical(raw);
  std::free(raw);
  if (!beneath_input_directory(canonical)) {
    error = "input device must resolve beneath /dev/input/: " + canonical;
    return false;
  }
  struct stat status{};
  if (::lstat(canonical.c_str(), &status) < 0) {
    error = "could not inspect input device '" + canonical +
            "': " + std::strerror(errno);
    return false;
  }
  if (!S_ISCHR(status.st_mode)) {
    error = "input device is not a character device: " + canonical;
    return false;
  }
  resolved = {std::move(canonical), static_cast<std::uint64_t>(status.st_dev),
              static_cast<std::uint64_t>(status.st_ino),
              static_cast<std::uint64_t>(status.st_rdev)};
  error.clear();
  return true;
}

bool validate_input_device_paths(const Options &options, std::string &error) {
  for (std::size_t index = 0; index < options.libinput_devices.size();
       ++index) {
    const auto &device = options.libinput_devices[index];
    if (!beneath_input_directory(device.canonical_path)) {
      error = "input device escaped /dev/input/: " + device.canonical_path;
      return false;
    }
    if (std::any_of(options.libinput_devices.begin(),
                    options.libinput_devices.begin() + index,
                    [&](const auto &prior) {
                      return prior.canonical_path == device.canonical_path;
                    })) {
      error = "duplicate input device: " + device.canonical_path;
      return false;
    }
    struct stat status{};
    if (::lstat(device.canonical_path.c_str(), &status) < 0 ||
        !same_identity(device, status)) {
      error = "input device identity changed before startup: " +
              device.canonical_path;
      return false;
    }
  }
  error.clear();
  return true;
}

ParseOptionsResult parse_options(int argc, char **argv, Options &options,
                                 std::ostream &output, std::ostream &error,
                                 InputDevicePathResolver resolve_device) {
  SeenOptions seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_usage(output);
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--version") {
      output << "glasswyrmd " << GW_PROJECT_VERSION << '\n';
      return ParseOptionsResult::ExitSuccess;
    }
    auto result =
        parse_base_argument(argument, index, argc, argv, options, error);
    if (result == ArgumentResult::NotHandled)
      result = parse_device_argument(argument, index, argc, argv, options,
                                     error, resolve_device);
    if (result == ArgumentResult::NotHandled)
      result =
          parse_xkb_argument(argument, index, argc, argv, options, seen, error);
    if (result == ArgumentResult::NotHandled)
      result = parse_repeat_argument(argument, index, argc, argv, options, seen,
                                     error);
    if (result == ArgumentResult::Parsed)
      continue;
    if (result == ArgumentResult::ExitFailure)
      return ParseOptionsResult::ExitFailure;
    error << "glasswyrmd: unknown option: " << argument << '\n';
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  return validate_options(options, resolve_device, error);
}

} // namespace glasswyrm::server
