#include "gwcomp/session_state_coordinator.hpp"
#include "tests/helpers/test_support.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace compositor = glasswyrm::compositor;

namespace {

class FakeSink final : public compositor::SessionStateRequestSink {
 public:
  bool enqueue(const gwipc_session_state_change &value,
               std::uint64_t &sequence, std::string &error) override {
    ++calls;
    last = value;
    if (fail) {
      error = "injected enqueue failure";
      return false;
    }
    sequence = next_sequence++;
    error.clear();
    return true;
  }

  gwipc_session_state_change last{};
  std::uint64_t next_sequence{41};
  std::size_t calls{};
  bool fail{};
};

gwipc_session_state_acknowledged ack(std::uint64_t generation,
                                     gwipc_session_state state,
                                     gwipc_session_state_result result) {
  gwipc_session_state_acknowledged value{};
  value.struct_size = sizeof(value);
  value.generation = generation;
  value.state = state;
  value.result = result;
  return value;
}

}  // namespace

int main() {
  using gw::test::require;
  using Clock = compositor::SessionStateTiming::Clock;
  auto now = Clock::time_point{};
  compositor::SessionStateTiming timing;
  timing.timeout = std::chrono::milliseconds(2000);
  timing.now = [&] { return now; };
  compositor::SessionStateCoordinator coordinator(timing);
  FakeSink sink;
  std::string error;

  require(compositor::session_wait_message_allowed(
              GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED) &&
              !compositor::session_wait_message_allowed(
                  GWIPC_MESSAGE_FRAME_COMMIT) &&
              !compositor::session_wait_message_allowed(
                  GWIPC_MESSAGE_OUTPUT_STATE_QUERY),
          "only the session acknowledgement may pass the coordination gate");

  coordinator.configure(false);
  require(!coordinator.enabled() && coordinator.timeout_ms() == -1 &&
              sink.calls == 0,
          "unnegotiated peer leaves historical VT path disabled");

  coordinator.configure(true);
  require(coordinator.state() == compositor::CoordinatedSessionState::Active &&
              coordinator.request_inactive(sink, error) &&
              sink.last.generation == 1 &&
              sink.last.state == GWIPC_SESSION_INACTIVE &&
              sink.last.flags == 0 && coordinator.generation() == 1 &&
              coordinator.timeout_ms() == 2000,
          "inactive request starts correlated bounded asynchronous wait");
  now += std::chrono::milliseconds(1999);
  require(coordinator.check_timeout(error) && coordinator.timeout_ms() == 1,
          "session timeout remains pending before existing bound");
  require(coordinator.acknowledge(
              41, ack(1, GWIPC_SESSION_INACTIVE,
                      GWIPC_SESSION_STATE_ACCEPTED),
              error) &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Inactive,
          "matching inactive acknowledgement permits VT release");

  require(coordinator.request_active(sink, error) &&
              sink.last.generation == 2 &&
              sink.last.state == GWIPC_SESSION_ACTIVE,
          "active request advances generation after display reacquire");
  require(coordinator.acknowledge(
              42, ack(2, GWIPC_SESSION_ACTIVE,
                      GWIPC_SESSION_STATE_ALREADY_APPLIED),
              error) &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Active,
          "already-applied active reply safely resumes producer flow");

  coordinator.configure(true);
  require(coordinator.request_inactive(sink, error) &&
              !coordinator.acknowledge(
                  999, ack(1, GWIPC_SESSION_INACTIVE,
                           GWIPC_SESSION_STATE_ACCEPTED),
                  error) &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Failed,
          "reply correlation mismatch is fatal");

  coordinator.configure(true);
  require(coordinator.request_inactive(sink, error) &&
              !coordinator.acknowledge(
                  44, ack(1, GWIPC_SESSION_INACTIVE,
                          GWIPC_SESSION_STATE_INPUT_UNAVAILABLE),
                  error) &&
              error.find("unavailable") != std::string::npos,
          "input-unavailable reply is fatal to real-input profile");

  coordinator.configure(true);
  require(coordinator.request_inactive(sink, error),
          "timeout scenario starts");
  now += std::chrono::milliseconds(2000);
  require(!coordinator.check_timeout(error) &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Failed &&
              error.find("timed out") != std::string::npos,
          "bounded session timeout becomes fatal");

  coordinator.configure(true);
  sink.fail = true;
  require(!coordinator.request_inactive(sink, error) &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Failed,
          "request enqueue failure is fatal");
  sink.fail = false;
  coordinator.configure(true);
  require(coordinator.request_inactive(sink, error),
          "disconnect scenario starts");
  coordinator.peer_disconnected();
  require(coordinator.state() == compositor::CoordinatedSessionState::Failed,
          "peer loss during handshake is fatal");
}
