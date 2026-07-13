#include "glasswyrmd/options.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

using glasswyrm::server::Options;
using glasswyrm::server::ParseOptionsResult;

ParseOptionsResult parse(std::vector<std::string> values, Options& options,
                         std::string& output, std::string& error) {
  std::vector<char*> arguments;
  arguments.reserve(values.size());
  for (auto& value : values) arguments.push_back(value.data());
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::server::parse_options(
      static_cast<int>(arguments.size()), arguments.data(), options,
      output_stream, error_stream);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

}  // namespace

int main() {
  std::string output;
  std::string error;
  Options options;
  if (parse({"glasswyrmd", "--display", "99", "--socket-dir", "/tmp/x",
             "--wm-socket", "/tmp/gwm.sock", "--compositor-socket",
             "/tmp/gwcomp.sock", "--software-content",
             "--synthetic-input-socket", "/tmp/input.sock"},
            options, output, error) != ParseOptionsResult::Run ||
      options.display != 99 || options.socket_dir != "/tmp/x" ||
      !options.integrated() || !options.software_content ||
      options.synthetic_input_socket != "/tmp/input.sock" ||
      *options.wm_socket != "/tmp/gwm.sock" ||
      *options.compositor_socket != "/tmp/gwcomp.sock") {
    return 1;
  }
  for (const auto& values : {
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm"},
           std::vector<std::string>{"glasswyrmd", "--compositor-socket",
                                    "/tmp/comp"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--wm-socket", "/tmp/b",
                                    "--compositor-socket", "/tmp/c"},
           std::vector<std::string>{"glasswyrmd", "--software-content"},
           std::vector<std::string>{"glasswyrmd", "--synthetic-input-socket",
                                    "/tmp/input.sock"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--compositor-socket", "/tmp/b",
                                    "--synthetic-input-socket", "/tmp/a",
                                    "--synthetic-input-socket", "/tmp/b"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--compositor-socket", "/tmp/b",
                                    "--software-content",
                                    "--software-content"},
       }) {
    options = {};
    if (parse(values, options, output, error) !=
        ParseOptionsResult::ExitFailure) {
      return 2;
    }
  }
  options = {};
  if (parse({"glasswyrmd"}, options, output, error) !=
          ParseOptionsResult::Run ||
      options.integrated()) {
    return 3;
  }
  options = {};
  if (parse({"glasswyrmd", "--help"}, options, output, error) !=
          ParseOptionsResult::ExitSuccess ||
      output.find("--wm-socket PATH --compositor-socket PATH") ==
          std::string::npos ||
      output.find("--software-content") == std::string::npos) {
    return 4;
  }
  if (output.find("--synthetic-input-socket PATH") == std::string::npos) {
    return 5;
  }
  return 0;
}
