#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace glasswyrm::compositor {

struct Options {
  std::string ipc_socket;
  std::string dump_dir;
  std::optional<std::string> scene_manifest;
  bool once{false};
  std::optional<std::uint64_t> max_frames;
};

enum class ParseOptionsResult { Run, ExitSuccess, ExitFailure };

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output, std::ostream& error);

}  // namespace glasswyrm::compositor
