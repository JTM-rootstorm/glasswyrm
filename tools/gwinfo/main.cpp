#include "output_client/output_client.hpp"
#include "config.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage:\n"
            "  gwinfo --socket PATH outputs [--vrr] [--json]\n"
            "  gwinfo --socket PATH windows [--vrr] [--json]\n"
            "  gwinfo --socket PATH all [--vrr] [--json]\n"
            "  gwinfo --socket PATH vrr [OUTPUT] [--json]\n"
            "  gwinfo --help\n"
            "  gwinfo --version\n";
}

} // namespace

int main(const int argc, char **argv) {
  using namespace glasswyrm::tools::output_client;
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    usage(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "gwinfo " << GW_PROJECT_VERSION << '\n';
    return 0;
  }
  std::string socket;
  std::string command;
  std::optional<std::string> selector;
  bool json = false;
  bool include_vrr = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket" && index + 1 < argc)
      socket = argv[++index];
    else if (argument == "--json")
      json = true;
    else if (argument == "--vrr")
      include_vrr = true;
    else if (command.empty() &&
             (argument == "outputs" || argument == "windows" ||
              argument == "all" || argument == "vrr"))
      command = argument;
    else if (command == "vrr" && !selector && !argument.starts_with("--"))
      selector = argument;
    else {
      std::cerr << "gwinfo: invalid argument '" << argument << "'\n";
      usage(std::cerr);
      return 2;
    }
  }
  if (socket.empty() || command.empty()) {
    std::cerr << "gwinfo: --socket PATH and a command are required\n";
    usage(std::cerr);
    return 2;
  }
  if (command == "vrr" && include_vrr) {
    std::cerr << "gwinfo: --vrr modifies outputs, windows, or all\n";
    usage(std::cerr);
    return 2;
  }
  std::uint32_t flags = 0;
  if (command != "windows")
    flags |= GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
             GWIPC_OUTPUT_QUERY_LAYOUT;
  if (command != "outputs")
    flags |= GWIPC_OUTPUT_QUERY_WINDOWS;
  if (command == "vrr")
    flags = GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_LAYOUT |
            GWIPC_OUTPUT_QUERY_WINDOWS | GWIPC_OUTPUT_QUERY_VRR;
  else if (include_vrr)
    flags |= GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_LAYOUT |
             GWIPC_OUTPUT_QUERY_VRR;
  Client client(socket);
  Snapshot snapshot;
  std::string error;
  if (!client.query(flags, snapshot, error)) {
    std::cerr << "gwinfo: " << error << '\n';
    return 1;
  }
  if (command == "vrr")
    print_vrr(snapshot,
              selector ? std::optional<std::string_view>(*selector)
                       : std::nullopt,
              json, std::cout);
  else if (command == "outputs")
    print_outputs(snapshot, json, std::cout);
  else if (command == "windows")
    print_windows(snapshot, json, std::cout);
  else
    print_all(snapshot, json, std::cout);
  return 0;
}
