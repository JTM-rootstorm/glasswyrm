#include "gwcomp/session_state_coordinator.hpp"
#include "gwcomp/runtime_session_gate.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
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

enum class PendingSessionMessage {
  FrameCommit,
  Acknowledgement,
};

void require_fifo_prefix_before_acknowledgement() {
  using gw::test::require;
  compositor::SessionStateCoordinator coordinator;
  FakeSink sink;
  std::string error;
  coordinator.configure(true);
  require(coordinator.request_inactive(sink, error) && coordinator.waiting(),
          "inactive request starts the FIFO regression wait");

  // A producer can have committed this message to the socket before it reads
  // the compositor's session request. The correlated acknowledgement must
  // therefore remain valid after the ordinary FIFO prefix is drained.
  constexpr std::array queued{
      PendingSessionMessage::FrameCommit,
      PendingSessionMessage::Acknowledgement,
  };
  require(compositor::session_wait_message_route(GWIPC_MESSAGE_FRAME_COMMIT) ==
                  compositor::SessionWaitMessageRoute::DrainContract &&
              compositor::session_wait_message_route(
                  GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED) ==
                  compositor::SessionWaitMessageRoute::Acknowledgement,
          "session wait routes the FIFO prefix before its acknowledgement");
  std::size_t drained_contracts = 0;
  for (const auto message : queued) {
    if (message == PendingSessionMessage::FrameCommit) {
      require(coordinator.waiting(),
              "queued frame is drained while session reply remains pending");
      ++drained_contracts;
      continue;
    }
    require(coordinator.acknowledge(
                41,
                ack(1, GWIPC_SESSION_INACTIVE,
                    GWIPC_SESSION_STATE_ACCEPTED),
                error),
            "correlated acknowledgement survives the queued frame prefix");
  }
  require(drained_contracts == 1 &&
              coordinator.state() ==
                  compositor::CoordinatedSessionState::Inactive,
          "FIFO frame prefix is bounded and session coordination completes");
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

  require_fifo_prefix_before_acknowledgement();

  require(!compositor::may_begin_vt_release(true, true, true, false, false,
                                            false) &&
              !compositor::vt_release_blocks_contract_service(true, false),
          "VT release waits for producer bootstrap while inventory remains "
          "serviceable");
  require(compositor::may_begin_vt_release(true, true, true, true, false,
                                           false) &&
              compositor::vt_release_blocks_contract_service(true, true),
          "VT release coordination begins only after producer bootstrap");
  require(!compositor::may_begin_vt_release(true, false, false, false, false,
                                            false) &&
              !compositor::vt_release_blocks_contract_service(true, false),
          "a pre-connection VT signal cannot suspend compositor bootstrap");

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
