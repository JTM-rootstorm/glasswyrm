#include "gwm/options.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

using glasswyrm::wm::Options;
using glasswyrm::wm::ParseOptionsResult;

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwm options test: %s\n", message);
  std::exit(1);
}

void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

ParseOptionsResult parse(std::vector<std::string> values, Options& options,
                         std::string& output, std::string& error) {
  std::vector<char*> arguments;
  arguments.reserve(values.size());
  for (auto& value : values) arguments.push_back(value.data());
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::wm::parse_options(
      static_cast<int>(arguments.size()), arguments.data(), options,
      output_stream, error_stream);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

} // namespace

int main() {
  std::string output;
  std::string error;
  Options options;
  require(parse({"gwm", "--ipc-socket", "/tmp/gwm.sock", "--once",
                 "--max-commits", "7"},
                options, output, error) == ParseOptionsResult::Run,
          "valid command line runs");
  require(options.ipc_socket == "/tmp/gwm.sock" && options.once &&
              options.max_commits == 7,
          "valid options are retained");

  options = {};
  require(parse({"gwm", "--help"}, options, output, error) ==
              ParseOptionsResult::ExitSuccess &&
              output.find("Usage: gwm --ipc-socket PATH") != std::string::npos,
          "help prints canonical usage");
  require(parse({"gwm", "--version"}, options, output, error) ==
              ParseOptionsResult::ExitSuccess && output.starts_with("gwm "),
          "version prints component identity");

  require(parse({"gwm"}, options, output, error) ==
              ParseOptionsResult::ExitFailure &&
              error.find("--ipc-socket is required") != std::string::npos,
          "socket path is required");
  require(parse({"gwm", "--ipc-socket", ""}, options, output, error) ==
              ParseOptionsResult::ExitFailure,
          "empty socket path is rejected");
  for (const auto* value : {"0", "-1", "12x", ""}) {
    require(parse({"gwm", "--ipc-socket", "/tmp/gwm.sock", "--max-commits",
                   value},
                  options, output, error) == ParseOptionsResult::ExitFailure,
            "invalid maximum commit count is rejected");
  }
  require(parse({"gwm", "--ipc-socket", "/tmp/gwm.sock", "--wat"}, options,
                output, error) == ParseOptionsResult::ExitFailure &&
              error.find("unknown option") != std::string::npos,
          "unknown options are rejected");
  return 0;
}
