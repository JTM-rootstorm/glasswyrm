#include "output_client/output_client.hpp"
#include "config.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using glasswyrm::tools::output_client::Edit;

void usage(std::ostream &output) {
  output << "Usage:\n"
            "  gwout --socket PATH list [--json]\n"
            "  gwout --socket PATH set OUTPUT [OPTIONS] [--json]\n"
            "Options:\n"
            "  --enable | --disable\n"
            "  --mode WIDTHxHEIGHT[@MILLIHZ]\n"
            "  --position X,Y\n"
            "  --scale NUM/DEN\n"
            "  --transform NAME\n"
            "  --vrr off|fullscreen|focused|app-requested|always-eligible\n"
            "  --primary\n"
            "  --help | --version\n";
}

bool take_value(const int argc, char **argv, int &index,
                std::string_view &value) {
  if (index + 1 >= argc)
    return false;
  value = argv[++index];
  return true;
}

bool has_edit(const Edit &edit) {
  return edit.enabled || edit.mode || edit.position || edit.scale ||
         edit.transform || edit.vrr_policy || edit.primary;
}

} // namespace

int main(const int argc, char **argv) {
  using namespace glasswyrm::tools::output_client;
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    usage(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "gwout " << GW_PROJECT_VERSION << '\n';
    return 0;
  }
  std::string socket;
  std::string command;
  std::string selector;
  Edit edit;
  bool json = false;
  bool parse_error = false;
  for (int index = 1; index < argc && !parse_error; ++index) {
    const std::string_view argument(argv[index]);
    std::string_view value;
    if (argument == "--socket" && take_value(argc, argv, index, value))
      socket = value;
    else if (argument == "--json")
      json = true;
    else if (command.empty() && (argument == "list" || argument == "set"))
      command = argument;
    else if (command == "set" && selector.empty() && !argument.starts_with("--"))
      selector = argument;
    else if (argument == "--enable" && !edit.enabled)
      edit.enabled = true;
    else if (argument == "--disable" && !edit.enabled)
      edit.enabled = false;
    else if (argument == "--primary")
      edit.primary = true;
    else if (argument == "--position" &&
             take_value(argc, argv, index, value)) {
      std::pair<std::int32_t, std::int32_t> position;
      parse_error = !parse_position(value, position);
      if (!parse_error) edit.position = position;
    } else if (argument == "--scale" &&
               take_value(argc, argv, index, value)) {
      std::pair<std::uint32_t, std::uint32_t> scale;
      parse_error = !parse_scale(value, scale);
      if (!parse_error) edit.scale = scale;
    } else if (argument == "--transform" &&
               take_value(argc, argv, index, value)) {
      edit.transform = parse_transform(value);
      parse_error = !edit.transform;
    } else if (argument == "--mode" &&
               take_value(argc, argv, index, value)) {
      std::pair<std::uint32_t, std::uint32_t> extent;
      std::optional<std::uint32_t> refresh;
      parse_error = !parse_mode(value, extent, refresh);
      if (!parse_error) {
        edit.mode = extent;
        edit.refresh_millihertz = refresh;
      }
    } else if (argument == "--vrr" &&
               take_value(argc, argv, index, value)) {
      edit.vrr_policy = parse_vrr_policy(value);
      parse_error = !edit.vrr_policy;
    } else {
      parse_error = true;
    }
  }
  if (parse_error || socket.empty() || command.empty() ||
      (command == "set" && (selector.empty() || !has_edit(edit))) ||
      (command == "list" && (has_edit(edit) || !selector.empty()))) {
    std::cerr << "gwout: invalid or incomplete command line\n";
    usage(std::cerr);
    return 2;
  }

  Client client(socket);
  Snapshot snapshot;
  std::string error;
  auto query_flags = GWIPC_OUTPUT_QUERY_DESCRIPTORS |
                     GWIPC_OUTPUT_QUERY_MODES |
                     GWIPC_OUTPUT_QUERY_LAYOUT;
  if (edit.vrr_policy) query_flags |= GWIPC_OUTPUT_QUERY_VRR;
  if (!client.query(query_flags, snapshot, error)) {
    std::cerr << "gwout: " << error << '\n';
    return 1;
  }
  if (command == "list") {
    print_outputs(snapshot, json, std::cout);
    return 0;
  }
  if (!apply_edit(snapshot, selector, edit, error)) {
    std::cerr << "gwout: " << error << '\n';
    return 1;
  }
  if (edit.vrr_policy &&
      !apply_vrr_edit(snapshot, selector, *edit.vrr_policy, error)) {
    std::cerr << "gwout: " << error << '\n';
    return 1;
  }
  gwipc_output_configuration_acknowledged acknowledgement{};
  if (!client.commit(snapshot, acknowledgement, error)) {
    std::cerr << "gwout: " << error << '\n';
    return 1;
  }
  print_acknowledgement(acknowledgement, json, std::cout);
  if (acknowledgement.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED) {
    if (edit.vrr_policy) {
      Snapshot applied;
      if (!client.query(query_flags, applied, error)) {
        std::cerr << "gwout: accepted configuration but could not query "
                     "effective VRR state: "
                  << error << '\n';
        return 1;
      }
      print_vrr(applied, selector, json, std::cout);
    }
    return 0;
  }
  if (acknowledgement.result == GWIPC_OUTPUT_CONFIGURATION_STALE_GENERATION)
    std::cerr << "gwout: stale layout generation; current generation is "
              << acknowledgement.applied_generation << '\n';
  else
    std::cerr << "gwout: output configuration rejected with result "
              << static_cast<unsigned>(acknowledgement.result) << '\n';
  return 1;
}
