#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace glasswyrm::server {

struct Options {
  std::uint16_t display = 0;
  std::string socket_dir = "/tmp/.X11-unix";
  std::optional<std::string> wm_socket;
  std::optional<std::string> compositor_socket;
  std::optional<std::string> synthetic_input_socket;
  std::optional<std::string> x11_trace;
  bool software_content = false;

  [[nodiscard]] bool integrated() const noexcept {
    return wm_socket.has_value() && compositor_socket.has_value();
  }
};

enum class ParseOptionsResult {
  Run,
  ExitSuccess,
  ExitFailure,
};

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output,
                                 std::ostream& error);

}  // namespace glasswyrm::server
