#include "gwcomp/session_state_coordinator.hpp"

#include <algorithm>
#include <limits>
#include <memory>

namespace glasswyrm::compositor {
namespace {

struct PayloadDeleter {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ContractDeleter {
  void operator()(gwipc_decoded_contract *value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

}  // namespace

void SessionStateCoordinator::configure(const bool negotiated) noexcept {
  state_ = negotiated ? CoordinatedSessionState::Active
                      : CoordinatedSessionState::Disabled;
  generation_ = 0;
  pending_sequence_ = 0;
  pending_state_ = GWIPC_SESSION_ACTIVE;
  deadline_ = {};
}

bool SessionStateCoordinator::request_inactive(SessionStateRequestSink &sink,
                                               std::string &error) {
  if (state_ != CoordinatedSessionState::Active) {
    error = "inactive session request requires coordinated active state";
    return false;
  }
  return request(GWIPC_SESSION_INACTIVE, sink, error);
}

bool SessionStateCoordinator::request_active(SessionStateRequestSink &sink,
                                             std::string &error) {
  if (state_ != CoordinatedSessionState::Inactive) {
    error = "active session request requires coordinated inactive state";
    return false;
  }
  return request(GWIPC_SESSION_ACTIVE, sink, error);
}

bool SessionStateCoordinator::request(const gwipc_session_state desired,
                                      SessionStateRequestSink &sink,
                                      std::string &error) {
  if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
    error = "session state generation exhausted";
    fail();
    return false;
  }
  gwipc_session_state_change change{};
  change.struct_size = sizeof(change);
  change.generation = generation_ + 1;
  change.state = desired;
  std::uint64_t sequence = 0;
  if (!sink.enqueue(change, sequence, error) || sequence == 0) {
    if (error.empty()) error = "session state request enqueue failed";
    fail();
    return false;
  }
  generation_ = change.generation;
  pending_sequence_ = sequence;
  pending_state_ = desired;
  deadline_ = timing_.now() + timing_.timeout;
  state_ = desired == GWIPC_SESSION_INACTIVE
               ? CoordinatedSessionState::AwaitingInactive
               : CoordinatedSessionState::AwaitingActive;
  error.clear();
  return true;
}

bool SessionStateCoordinator::acknowledge(
    const std::uint64_t reply_to,
    const gwipc_session_state_acknowledged &acknowledged,
    std::string &error) {
  if (!waiting() || reply_to != pending_sequence_ ||
      acknowledged.generation != generation_ ||
      acknowledged.state != pending_state_) {
    error = "session state acknowledgement correlation mismatch";
    fail();
    return false;
  }
  if (acknowledged.result != GWIPC_SESSION_STATE_ACCEPTED &&
      acknowledged.result != GWIPC_SESSION_STATE_ALREADY_APPLIED) {
    error = acknowledged.result == GWIPC_SESSION_STATE_INPUT_UNAVAILABLE
                ? "protocol server input is unavailable"
                : "protocol server rejected session state";
    fail();
    return false;
  }
  state_ = pending_state_ == GWIPC_SESSION_INACTIVE
               ? CoordinatedSessionState::Inactive
               : CoordinatedSessionState::Active;
  pending_sequence_ = 0;
  error.clear();
  return true;
}

bool SessionStateCoordinator::check_timeout(std::string &error) {
  if (!waiting() || timing_.now() < deadline_) return true;
  error = "session state acknowledgement timed out";
  fail();
  return false;
}

void SessionStateCoordinator::peer_disconnected() noexcept {
  state_ = waiting() ? CoordinatedSessionState::Failed
                     : CoordinatedSessionState::Disabled;
  pending_sequence_ = 0;
}

int SessionStateCoordinator::timeout_ms() const noexcept {
  if (!waiting()) return -1;
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline_ - timing_.now());
  if (remaining.count() <= 0) return 0;
  return static_cast<int>(std::min<std::int64_t>(remaining.count(),
                                                 std::numeric_limits<int>::max()));
}

bool GwipcSessionStateRequestSink::enqueue(
    const gwipc_session_state_change &change, std::uint64_t &sequence,
    std::string &error) {
  gwipc_contract_payload *raw_payload = nullptr;
  const auto encode =
      gwipc_contract_encode_session_state_change(&change, &raw_payload);
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw_payload);
  if (encode != GWIPC_STATUS_OK) {
    error = "encode session state request failed";
    return false;
  }
  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_SESSION_STATE_CHANGE;
  outgoing.flags = GWIPC_FLAG_ACK_REQUIRED;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  const auto status =
      gwipc_connection_enqueue_with_sequence(connection_, &outgoing, &sequence);
  if (status != GWIPC_STATUS_OK) {
    error = "enqueue session state request failed: " +
            std::string(gwipc_status_string(status));
    return false;
  }
  error.clear();
  return true;
}

bool decode_session_state_acknowledgement(
    const gwipc_message *message, gwipc_session_state_acknowledged &value,
    std::uint64_t &reply_to, std::string &error) {
  if (message == nullptr ||
      gwipc_message_type(message) != GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED ||
      gwipc_message_flags(message) != GWIPC_FLAG_REPLY) {
    error = "expected session state acknowledgement reply";
    return false;
  }
  gwipc_decoded_contract *raw_contract = nullptr;
  const auto status = gwipc_contract_decode_message(message, &raw_contract);
  std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(raw_contract);
  if (status != GWIPC_STATUS_OK) {
    error = "decode session state acknowledgement failed";
    return false;
  }
  const auto *decoded =
      gwipc_decoded_session_state_acknowledged(contract.get());
  if (decoded == nullptr) {
    error = "session state acknowledgement payload is missing";
    return false;
  }
  value = *decoded;
  reply_to = gwipc_message_reply_to(message);
  error.clear();
  return true;
}

}  // namespace glasswyrm::compositor
