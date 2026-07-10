#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace glasswyrm::server {

struct Options {
  std::uint16_t display = 0;
  std::string socket_dir = "/tmp/.X11-unix";
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
