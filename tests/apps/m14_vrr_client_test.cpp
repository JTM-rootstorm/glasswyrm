#include "m14_vrr_client_options.hpp"
#include "m14_vrr_client_support.hpp"

#include "helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace gw::test::m14;
using gw::test::require;

bool parses(std::vector<std::string> arguments, ClientOptions &options) {
  std::vector<char *> pointers;
  pointers.reserve(arguments.size());
  for (auto &argument : arguments)
    pointers.push_back(argument.data());
  return parse_client_options(static_cast<int>(pointers.size()),
                              pointers.data(), options);
}

bool parses(std::vector<std::string> arguments) {
  ClientOptions options;
  return parses(std::move(arguments), options);
}

void test_options() {
  for (const auto *mode :
       {"fullscreen", "borderless", "windowed", "app-requested", "preference", "cadence"}) {
    require(
        parses({"client", "--display", ":4", "--mode", mode, "--result",
                "/tmp/result", "--target-refresh-hz", "72", "--hold-ms", "0"}),
        "all six fixed client modes parse");
  }
  ClientOptions options;
  require(parses({"client", "--display", ":4", "--mode", "cadence", "--result",
                  "/tmp/result"},
                 options) &&
              options.frame_count == 180 && !options.prefer,
          "cadence uses a bounded default run without implicit app request");
  options = {};
  require(parses({"client", "--display", ":4", "--mode", "app-requested",
                  "--result", "/tmp/result"},
                 options) &&
              options.prefer,
          "app-requested mode explicitly selects Prefer");
  require(parses({"client", "--display", ":4", "--mode", "windowed", "--result",
                  "/tmp/result", "--frames", "1"}),
          "non-cadence mode accepts its single published frame");
  for (const auto *preference : {"default", "disable", "allow", "prefer"}) {
    options = {};
    require(parses({"client", "--display", ":4", "--mode", "windowed",
                    "--result", "/tmp/result", "--preference", preference},
                   options) &&
                options.preference_set &&
                client_preference_name(options.preference) != std::string_view{},
            "all GW_VRR preferences parse explicitly");
  }
  require(!parses({"client", "--display", ":4", "--mode", "unknown", "--result",
                   "/tmp/result"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--frames", "0"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--frames", "10001"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--hold-ms", "60001"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--preference", "bogus"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--prefer", "--preference",
                       "prefer"}) &&
              !parses({"client", "--display", ":4", "--mode", "preference",
                       "--result", "/tmp/result", "--preference", "prefer"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--frames", "2"}) &&
              parses({"client", "--self-test"}),
          "client options reject unbounded or incomplete invocations");
}

void test_cadence_and_pixels() {
  require(target_interval_nanoseconds(0) == 0 &&
              target_interval_nanoseconds(72) == 13'888'888,
          "target cadence uses deterministic integer nanoseconds");
  std::uint64_t deadline{};
  require(absolute_deadline(100, 25, 0, deadline) && deadline == 125 &&
              absolute_deadline(100, 25, 3, deadline) && deadline == 200 &&
              !absolute_deadline(100, 0, 0, deadline) &&
              !absolute_deadline(std::numeric_limits<std::uint64_t>::max() - 2,
                                 2, 1, deadline),
          "absolute deadlines are indexed from one and reject overflow");

  const auto pattern = deterministic_pattern(16, 16);
  require(pattern.size() == 256 && pattern.front() == UINT32_C(0x00ff4040) &&
              pattern[1] == UINT32_C(0x00e0f020) &&
              pattern[15] == UINT32_C(0x00ff4040),
          "base XRGB pattern is exact");
  const auto even = deterministic_damage(0);
  const auto odd = deterministic_damage(1);
  require(even.size() ==
                  static_cast<std::size_t>(kDamageWidth) * kDamageHeight &&
              even.size() == odd.size() && even.front() != odd.front() &&
              even[4] != odd[4] && even.front() == odd[4],
          "cadence alternates only the fixed bounded rectangle");
  EventfdDamageProducer producer;
  require(producer.produce(0) == even && producer.produce(1) == odd,
          "eventfd producer synchronizes exact cadence damage publication");
}

void test_state_json_and_private_publish() {
  const ClientState state{
      ClientMode::Cadence, 42, 640, 480, true, true, false, 120, 72,
      13'888'888, ClientPreference::Prefer, true, 4, 3, 7,
      UINT64_C(0x1020), true};
  const auto json = client_state_json(state);
  require(json.find("\"schema\": \"glasswyrm.m14-vrr-client.v2\"") !=
                  std::string::npos &&
              json.find("\"mode\": \"cadence\"") != std::string::npos &&
              json.find("\"preference\": \"Prefer\"") != std::string::npos &&
              json.find("\"preference_reply_count\": 4") != std::string::npos &&
              json.find("\"notify_event_count\": 3") != std::string::npos &&
              json.find("\"reason_mask\": 4128") != std::string::npos &&
              json.find("\"eventfd_synchronized\": true") != std::string::npos &&
              json.find("\"preference_sequence\": []") != std::string::npos &&
              json.find("\"cadence_absolute_monotonic\": true") !=
                  std::string::npos &&
              json.find("timestamp") == std::string::npos,
          "client-state JSON is stable and excludes nondeterministic clocks");

  std::string pattern = "/tmp/glasswyrm-m14-vrr-client-XXXXXX";
  require(::mkdtemp(pattern.data()) != nullptr,
          "create client-state test directory");
  const auto root = std::filesystem::path(pattern);
  const auto target = root / "state.json";
  try {
    write_client_state(target.string(), state);
    struct stat status{};
    require(::lstat(target.c_str(), &status) == 0 && S_ISREG(status.st_mode) &&
                (status.st_mode & 0777U) == 0600U,
            "client-state output is a private regular file");
    std::ifstream input(target);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    require(contents == json, "published client state is byte-deterministic");
    bool rejected_existing = false;
    try {
      write_client_state(target.string(), state);
    } catch (const std::runtime_error &) {
      rejected_existing = true;
    }
    require(rejected_existing, "client state never replaces existing evidence");
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
}

} // namespace

int main() {
  test_options();
  test_cadence_and_pixels();
  test_state_json_and_private_publish();
  return 0;
}
