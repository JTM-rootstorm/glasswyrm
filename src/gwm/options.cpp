#include "gwm/options.hpp"

#include "config.hpp"

#include <charconv>
#include <ostream>
#include <string_view>

namespace glasswyrm::wm {
namespace {

void print_usage(std::ostream& output) {
  output << "Usage: gwm --ipc-socket PATH [--once] [--max-commits N] "
            "[--help] [--version]\n";
}

bool parse_positive(std::string_view text, std::uint64_t& value) {
  if (text.empty()) return false;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

} // namespace

ParseOptionsResult parse_options(int argc, char** argv, Options& options,
                                 std::ostream& output, std::ostream& error) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_usage(output);
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--version") {
      output << "gwm " << GW_PROJECT_VERSION << '\n';
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--ipc-socket") {
      if (++index >= argc || argv[index][0] == '\0') {
        error << "gwm: --ipc-socket requires a non-empty path\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.ipc_socket = argv[index];
      continue;
    }
    if (argument == "--once") {
      options.once = true;
      continue;
    }
    if (argument == "--max-commits") {
      std::uint64_t count = 0;
      if (++index >= argc || !parse_positive(argv[index], count)) {
        error << "gwm: --max-commits requires a positive integer\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.max_commits = count;
      continue;
    }
    error << "gwm: unknown option: " << argument << '\n';
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  if (options.ipc_socket.empty()) {
    error << "gwm: --ipc-socket is required\n";
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  return ParseOptionsResult::Run;
}

} // namespace glasswyrm::wm
