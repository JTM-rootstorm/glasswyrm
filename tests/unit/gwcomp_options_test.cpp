#include "gwcomp/options.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

using glasswyrm::compositor::Options;
using glasswyrm::compositor::ParseOptionsResult;

ParseOptionsResult parse(std::vector<std::string> arguments, Options& options,
                         std::string& output, std::string& error) {
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::compositor::parse_options(
      static_cast<int>(argv.size()), argv.data(), options, output_stream,
      error_stream);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

}  // namespace

int main() {
  Options options;
  std::string output;
  std::string error;
  if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
             "/tmp/frames", "--once", "--max-frames", "12"},
            options, output, error) != ParseOptionsResult::Run ||
      options.ipc_socket != "/run/gw.sock" ||
      options.dump_dir != "/tmp/frames" || !options.once ||
      options.max_frames != 12 || !output.empty() || !error.empty())
    return 1;

  options = {};
  if (parse({"gwcomp", "--help"}, options, output, error) !=
          ParseOptionsResult::ExitSuccess ||
      output.find("Usage: gwcomp") == std::string::npos)
    return 1;

  options = {};
  if (parse({"gwcomp"}, options, output, error) !=
          ParseOptionsResult::ExitFailure ||
      error.find("are required") == std::string::npos)
    return 1;

  for (const auto* invalid : {"0", "-1", "abc", "12x"}) {
    options = {};
    if (parse({"gwcomp", "--ipc-socket", "/run/gw.sock", "--dump-dir",
               "/tmp/frames", "--max-frames", invalid},
              options, output, error) != ParseOptionsResult::ExitFailure)
      return 1;
  }

  return 0;
}
