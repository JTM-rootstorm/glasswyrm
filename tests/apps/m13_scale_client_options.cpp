#include "m13_scale_client_options.hpp"

#include <climits>
#include <stdexcept>
#include <string>
#include <vector>

namespace gw::test::m13 {
namespace {

bool parse_number(const std::string_view text, int& value) {
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoi(std::string(text), &consumed);
    if (consumed != text.size()) return false;
    value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_hold_ms(const std::string_view text, EvidenceHold& hold) {
  return parse_number(text, hold.milliseconds) && hold.milliseconds >= 1 &&
         hold.milliseconds <= 60'000;
}

bool parses(std::vector<std::string> arguments) {
  std::vector<char*> pointers;
  pointers.reserve(arguments.size());
  for (auto& argument : arguments) pointers.push_back(argument.data());
  ClientOptions options;
  return parse_options(static_cast<int>(pointers.size()), pointers.data(),
                       options);
}

}  // namespace

bool parse_options(const int argc, char** argv, ClientOptions& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      options.help = true;
    } else if (argument == "--self-test") {
      options.self_test = true;
    } else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':')
        options.display.erase(0, 1);
    } else if (argument == "--socket-dir" && index + 1 < argc) {
      options.socket_dir = argv[++index];
    } else if (argument == "--result" && index + 1 < argc) {
      options.result_path = argv[++index];
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little")
        options.order = protocol::x11::ByteOrder::LittleEndian;
      else if (value == "big")
        options.order = protocol::x11::ByteOrder::BigEndian;
      else
        return false;
    } else if (argument == "--move-x" && index + 1 < argc) {
      int value = 0;
      if (!parse_number(argv[++index], value) || value < INT16_MIN ||
          value > INT16_MAX)
        return false;
      options.move_x = static_cast<std::int16_t>(value);
    } else if (argument == "--timeout-ms" && index + 1 < argc) {
      if (!parse_number(argv[++index], options.timeout_ms) ||
          options.timeout_ms <= 0)
        return false;
    } else if (argument == "--hold-ms" && index + 1 < argc) {
      if (!parse_hold_ms(argv[++index], options.moved_hold)) return false;
    } else if (argument == "--ready-file" && index + 1 < argc) {
      options.moved_hold.ready_file = argv[++index];
      if (options.moved_hold.ready_file.empty()) return false;
    } else if (argument == "--initial-hold-ms" && index + 1 < argc) {
      if (!parse_hold_ms(argv[++index], options.initial_hold)) return false;
    } else if (argument == "--initial-ready-file" && index + 1 < argc) {
      options.initial_hold.ready_file = argv[++index];
      if (options.initial_hold.ready_file.empty()) return false;
    } else {
      return false;
    }
  }
  return options.initial_hold.disabled_or_valid() &&
         options.moved_hold.disabled_or_valid() &&
         (options.help || options.self_test || !options.display.empty());
}

void self_test_options() {
  if (!parses({"client", "--self-test"}) ||
      !parses({"client", "--self-test", "--initial-hold-ms", "1",
               "--initial-ready-file", "/tmp/initial", "--hold-ms",
               "60000", "--ready-file", "/tmp/moved"}) ||
      parses({"client", "--self-test", "--hold-ms", "1"}) ||
      parses({"client", "--self-test", "--ready-file", "/tmp/moved"}) ||
      parses({"client", "--self-test", "--initial-hold-ms", "0",
              "--initial-ready-file", "/tmp/initial"}) ||
      parses({"client", "--self-test", "--initial-hold-ms", "60001",
              "--initial-ready-file", "/tmp/initial"}))
    throw std::runtime_error("evidence hold option parsing self-test failed");
}

}  // namespace gw::test::m13
