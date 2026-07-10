#include "glasswyrmd/options.hpp"

#include "config.hpp"

#include <charconv>
#include <ostream>
#include <string_view>

namespace glasswyrm::server {
namespace {

void print_usage(std::ostream& output) {
  output << "Usage: glasswyrmd [--display N] [--socket-dir PATH] [--help] "
            "[--version]\n";
}

bool parse_display(std::string_view text, std::uint16_t& display) {
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
      output << "glasswyrmd " << GW_PROJECT_VERSION << '\n';
      return ParseOptionsResult::ExitSuccess;
    }
    if (argument == "--display") {
      if (++index >= argc || !parse_display(argv[index], options.display)) {
        error << "glasswyrmd: --display requires an integer from 0 to 65535\n";
        return ParseOptionsResult::ExitFailure;
      }
      continue;
    }
    if (argument == "--socket-dir") {
      if (++index >= argc || argv[index][0] == '\0') {
        error << "glasswyrmd: --socket-dir requires a non-empty path\n";
        return ParseOptionsResult::ExitFailure;
      }
      options.socket_dir = argv[index];
      continue;
    }
    error << "glasswyrmd: unknown option: " << argument << '\n';
    print_usage(error);
    return ParseOptionsResult::ExitFailure;
  }
  return ParseOptionsResult::Run;
}

}  // namespace glasswyrm::server
