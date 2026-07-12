#include "gwcomp/options.hpp"

#include "config.hpp"

#include <charconv>
#include <ostream>
#include <string_view>

namespace glasswyrm::compositor {
namespace {

void print_usage(std::ostream& output) {
  output << "Usage: gwcomp --ipc-socket PATH --dump-dir PATH [--once] "
            "[--max-frames N] [--help] [--version]\n";
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

  if (options.ipc_socket.empty() || options.dump_dir.empty()) {
    error << "gwcomp: --ipc-socket and --dump-dir are required\n";
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  return ParseOptionsResult::Run;
}

}  // namespace glasswyrm::compositor
