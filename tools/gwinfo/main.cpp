#include "output_client/output_client.hpp"
#include "config.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage:\n"
            "  gwinfo --socket PATH outputs [--json]\n"
            "  gwinfo --socket PATH windows [--json]\n"
            "  gwinfo --socket PATH all [--json]\n"
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
  bool json = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket" && index + 1 < argc)
      socket = argv[++index];
    else if (argument == "--json")
      json = true;
    else if (command.empty() &&
             (argument == "outputs" || argument == "windows" ||
              argument == "all"))
      command = argument;
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
  std::uint32_t flags = 0;
  if (command != "windows")
    flags |= GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
             GWIPC_OUTPUT_QUERY_LAYOUT;
  if (command != "outputs")
    flags |= GWIPC_OUTPUT_QUERY_WINDOWS;
  Client client(socket);
  Snapshot snapshot;
  std::string error;
  if (!client.query(flags, snapshot, error)) {
    std::cerr << "gwinfo: " << error << '\n';
    return 1;
  }
  if (command == "outputs")
    print_outputs(snapshot, json, std::cout);
  else if (command == "windows")
    print_windows(snapshot, json, std::cout);
  else
    print_all(snapshot, json, std::cout);
  return 0;
}
