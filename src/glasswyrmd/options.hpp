#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::server {

struct InputDevicePath {
  std::string canonical_path;
  std::uint64_t containing_device{};
  std::uint64_t inode{};
  std::uint64_t special_device{};
};

using InputDevicePathResolver = bool (*)(std::string_view, InputDevicePath &,
                                         std::string &);

struct Options {
  std::uint16_t display = 0;
  std::string socket_dir = "/tmp/.X11-unix";
  std::optional<std::string> wm_socket;
  std::optional<std::string> compositor_socket;
  std::optional<std::string> synthetic_input_socket;
  std::optional<std::string> x11_trace;
  std::vector<InputDevicePath> libinput_devices;
  std::string xkb_rules = "evdev";
  std::string xkb_model = "pc105";
  std::string xkb_layout = "us";
  std::string xkb_variant;
  std::string xkb_options;
  std::uint32_t repeat_delay_ms = 500;
  std::uint32_t repeat_rate_hz = 25;
  bool software_content = false;
  bool game_compat = false;
  bool output_model = false;
  bool scale_protocol = false;
  std::vector<std::string> disabled_extensions;

  [[nodiscard]] bool integrated() const noexcept {
    return wm_socket.has_value() && compositor_socket.has_value();
  }
  [[nodiscard]] bool real_input_enabled() const noexcept {
    return !libinput_devices.empty();
  }
};

enum class ParseOptionsResult {
  Run,
  ExitSuccess,
  ExitFailure,
};

[[nodiscard]] bool resolve_input_device_path(std::string_view path,
                                             InputDevicePath &resolved,
                                             std::string &error);
[[nodiscard]] bool validate_input_device_paths(const Options &options,
                                               std::string &error);

ParseOptionsResult parse_options(
    int argc, char **argv, Options &options, std::ostream &output,
    std::ostream &error,
    InputDevicePathResolver resolve_device = resolve_input_device_path);

} // namespace glasswyrm::server
