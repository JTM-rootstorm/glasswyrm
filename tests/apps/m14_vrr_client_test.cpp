#include "m14_vrr_client_options.hpp"
#include "m14_vrr_client_support.hpp"

#include "helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <deque>
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
        parses(mode == std::string_view("cadence")
                   ? std::vector<std::string>{
                         "client", "--display", ":4", "--mode", mode,
                         "--result", "/tmp/result", "--target-refresh-hz",
                         "72", "--hold-ms", "0", "--control-socket",
                         "/tmp/control", "--output", "DP-1"}
                   : std::vector<std::string>{
                         "client", "--display", ":4", "--mode", mode,
                         "--result", "/tmp/result", "--target-refresh-hz",
                         "72", "--hold-ms", "0"}),
        "all six fixed client modes parse");
  }
  ClientOptions options;
  require(parses({"client", "--display", ":4", "--mode", "cadence", "--result",
                  "/tmp/result", "--control-socket", "/tmp/control",
                  "--output", "DP-1"},
                 options) &&
              options.frame_count == 180 && !options.prefer,
          "cadence uses a bounded default run without implicit app request");
  options = {};
  require(parses({"client", "--display", ":4", "--mode", "app-requested",
                  "--result", "/tmp/result"},
                 options) &&
              options.prefer,
          "app-requested mode explicitly selects Prefer");
  options = {};
  require(parses({"client", "--display", ":4", "--mode", "windowed",
                  "--result", "/tmp/result", "--hold-ms", "1000",
                  "--repaint-trigger", "/tmp/repaint", "--repaint-count", "2"},
                 options) &&
              options.repaint_trigger == "/tmp/repaint" &&
              options.repaint_count == 2,
          "bounded repaint trigger parses explicitly");
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
                       "--result", "/tmp/result", "--frames", "0",
                       "--control-socket", "/tmp/control", "--output", "DP-1"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--frames", "10001",
                       "--control-socket", "/tmp/control", "--output", "DP-1"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--hold-ms", "60001",
                       "--control-socket", "/tmp/control", "--output", "DP-1"}) &&
              !parses({"client", "--display", ":4", "--mode", "cadence",
                       "--result", "/tmp/result", "--control-socket",
                       "/tmp/control"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--control-socket",
                       "/tmp/control", "--output", "DP-1"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--preference", "bogus"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--prefer", "--preference",
                       "prefer"}) &&
              !parses({"client", "--display", ":4", "--mode", "preference",
                       "--result", "/tmp/result", "--preference", "prefer"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--frames", "2"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--repaint-trigger",
                       "/tmp/repaint"}) &&
              !parses({"client", "--display", ":4", "--mode", "windowed",
                       "--result", "/tmp/result", "--hold-ms", "0",
                       "--repaint-trigger", "/tmp/repaint", "--repaint-count", "2"}) &&
              parses({"client", "--self-test"}),
          "client options reject unbounded or incomplete invocations");
}

void test_cadence_and_pixels() {
  require(
      kPresentationQueryFlags ==
          (GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_LAYOUT |
           GWIPC_OUTPUT_QUERY_WINDOWS | GWIPC_OUTPUT_QUERY_VRR),
      "presentation queries include window state needed to validate a candidate");
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
  bool rejected_unprepared = false;
  try {
    (void)producer.produce(0);
  } catch (const std::runtime_error&) {
    rejected_unprepared = true;
  }
  producer.prepare(0);
  bool rejected_overlap = false;
  try {
    producer.prepare(1);
  } catch (const std::runtime_error&) {
    rejected_overlap = true;
  }
  const auto produced_even = producer.produce(0);
  producer.prepare(1);
  require(rejected_unprepared && rejected_overlap && produced_even == even &&
              producer.produce(1) == odd,
          "eventfd producer precomputes one exact cadence frame at a time");
}

void test_presentation_pacer() {
  static_assert(kPresentationPollNanoseconds == 500'000);
  PresentationPacer pacer(3, 10, 50);
  std::string error;
  require(pacer.begin({7, 9}, 100, error), "pacer accepts an initial marker");
  require(pacer.next(109).action == PresentationPacerAction::Wait &&
              pacer.next(110).action == PresentationPacerAction::SubmitNow &&
              pacer.submitted(1, 110, error),
          "pacer waits for an absolute deadline before one submission");
  require(pacer.observe({PresentationObservationKind::Complete, {7, 9}, 111,
                         {}}).action == PresentationPacerAction::Wait &&
              pacer.observe({PresentationObservationKind::RetryableNotReady,
                              {}, 112, {}}).action ==
                  PresentationPacerAction::Wait &&
              pacer.observe({PresentationObservationKind::Complete, {8, 10},
                              113, {}}).action ==
                  PresentationPacerAction::FramePresented,
          "duplicate and retryable observations do not count presentations");
  require(pacer.next(145).action == PresentationPacerAction::MissedDeadline &&
              pacer.next(145).action == PresentationPacerAction::MissedDeadline &&
              pacer.next(145).action == PresentationPacerAction::MissedDeadline &&
              pacer.next(145).action == PresentationPacerAction::Wait &&
              pacer.next(150).action == PresentationPacerAction::SubmitNow &&
              pacer.submitted(2, 150, error),
          "late work skips absolute slots instead of burst catch-up");
  require(pacer.observe({PresentationObservationKind::Complete, {9, 11}, 155,
                         {}}).action == PresentationPacerAction::FramePresented &&
              pacer.next(160).action == PresentationPacerAction::SubmitNow &&
              pacer.submitted(3, 160, error) &&
              pacer.observe({PresentationObservationKind::Complete, {10, 12},
                              166, {}}).action ==
                  PresentationPacerAction::FramePresented &&
              pacer.next(166).action == PresentationPacerAction::Complete,
          "pacer completes only after every submitted frame is presented");
  const auto &stats = pacer.stats();
  require(stats.scheduled_frame_count == 3 &&
              stats.submitted_frame_count == 3 &&
              stats.presented_frame_count == 3 &&
              stats.maximum_outstanding_updates == 1 &&
              stats.retryable_query_count == 1 &&
              stats.missed_deadline_count == 3 &&
              stats.maximum_completion_latency_nanoseconds == 6,
          "pacer publishes bounded deterministic counters");
}

void test_presentation_pacer_failures() {
  std::string error;
  PresentationPacer regression(1, 10, 50);
  require(regression.begin({10, 20}, 100, error) &&
              regression.next(110).action == PresentationPacerAction::SubmitNow &&
              regression.submitted(1, 110, error) &&
              regression.observe({PresentationObservationKind::Complete,
                                  {11, 19}, 111, {}}).action ==
                  PresentationPacerAction::Fatal,
          "generation regression is fatal");

  PresentationPacer fatal_query(1, 10, 50);
  require(fatal_query.begin({1, 1}, 100, error) &&
              fatal_query.next(110).action == PresentationPacerAction::SubmitNow &&
              fatal_query.submitted(1, 110, error) &&
              fatal_query.observe({PresentationObservationKind::Fatal, {}, 111,
                                   "peer closed"}).action ==
                  PresentationPacerAction::Fatal,
          "fatal observer errors fail immediately");

  PresentationPacer timeout(1, 10, 5);
  require(timeout.begin({1, 1}, 100, error) &&
              timeout.next(110).action == PresentationPacerAction::SubmitNow &&
              timeout.submitted(1, 110, error) &&
              timeout.observe({PresentationObservationKind::Complete, {1, 1},
                               115, {}}).action ==
                  PresentationPacerAction::Timeout,
          "a stalled marker times out at the bounded deadline");
}

class FakeObserver final : public PresentationObserver {
public:
  explicit FakeObserver(std::deque<PresentationObservation> values)
      : values_(std::move(values)) {}

  PresentationObservation observe() override {
    require(!values_.empty(), "fake observer has a queued result");
    auto value = std::move(values_.front());
    values_.pop_front();
    return value;
  }

private:
  std::deque<PresentationObservation> values_;
};

void test_injected_presentation_observer() {
  std::string error;
  PresentationPacer pacer(1, 10, 50);
  FakeObserver observer({
      {PresentationObservationKind::RetryableNotReady, {}, 111, {}},
      {PresentationObservationKind::Complete, {2, 2}, 112, {}},
  });
  require(pacer.begin({1, 1}, 100, error) &&
              pacer.next(110).action == PresentationPacerAction::SubmitNow &&
              pacer.submitted(1, 110, error) &&
              observe_presentation(pacer, observer).action ==
                  PresentationPacerAction::Wait &&
              observe_presentation(pacer, observer).action ==
                  PresentationPacerAction::FramePresented,
          "injected observer drives the transport-neutral pacer");
}

void test_state_json_and_private_publish() {
  const ClientState state{
      ClientMode::Cadence, 42, 640, 480, true, true, false, 120, 72,
      13'888'888, ClientPreference::Prefer, true, 4, 3, 7,
      UINT64_C(0x1020), true, "DP-1",
      {120, 120, 119, {40, 50}, {160, 170}, 1, 3, 2, 9000}, true};
  const auto json = client_state_json(state);
  require(json.find("\"schema\": \"glasswyrm.m14-vrr-client.v3\"") !=
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
              json.find("\"selected_output\": \"DP-1\"") !=
                  std::string::npos &&
              json.find("\"presented_frame_count\": 119") !=
                  std::string::npos &&
              json.find("\"maximum_outstanding_updates\": 1") !=
                  std::string::npos &&
              json.find("\"presentation_paced\": true") !=
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
  test_presentation_pacer();
  test_presentation_pacer_failures();
  test_injected_presentation_observer();
  test_state_json_and_private_publish();
  return 0;
}
