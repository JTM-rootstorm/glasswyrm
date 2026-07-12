#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace glasswyrm::wm {

struct Options {
  std::string ipc_socket;
  bool once{false};
  std::optional<std::uint64_t> max_commits;
};

enum class ParseOptionsResult { Run, ExitSuccess, ExitFailure };

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output, std::ostream& error);

} // namespace glasswyrm::wm
