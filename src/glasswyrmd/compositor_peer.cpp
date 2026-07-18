#include "glasswyrmd/compositor_peer.hpp"

#include "glasswyrmd/compositor_buffer_replay.hpp"

#include <algorithm>
#include <memory>
#include <set>

namespace glasswyrm::server {
namespace {
constexpr gwipc_capabilities kMetadataCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT |
    GWIPC_CAP_WINDOW_LIFECYCLE;
constexpr gwipc_capabilities kContentCapabilities =
    kMetadataCapabilities | GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_CURSOR_SURFACE;
struct ContractDelete {
  void operator()(gwipc_contract_payload *p) const {
    gwipc_contract_payload_destroy(p);
  }
};
struct DecodedDelete {
  void operator()(gwipc_decoded_contract *p) const {
    gwipc_decoded_contract_destroy(p);
  }
};
template <class T, class Encoder>
bool enqueue_contract(gwipc_connection *connection, std::uint16_t type,
                      std::uint32_t flags, const T &value, Encoder encoder,
                      std::uint64_t *out_sequence = nullptr,
                      std::uint64_t reply_to = 0) {
  gwipc_contract_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  message.reply_to = reply_to;
  if (out_sequence)
    return gwipc_connection_enqueue_with_sequence(
               connection, &message, out_sequence) == GWIPC_STATUS_OK;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
void forget_cursor(CompositorSnapshotSubmission& submission) {
  std::set<std::uint64_t> cursor_surfaces;
  for (const auto& surface : submission.surfaces)
    if (surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR)
      cursor_surfaces.insert(surface.surface_id);
  std::erase_if(submission.surfaces, [](const auto& surface) {
    return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  });
  std::erase_if(submission.buffers, [&](const auto& buffer) {
    return cursor_surfaces.contains(buffer.attach.surface_id);
  });
  std::erase_if(submission.damages, [&](const auto& damage) {
    return cursor_surfaces.contains(damage.surface_id);
  });
}

} // namespace

CompositorPeer::CompositorPeer(std::string path,
                               const gw::protocol::x11::ScreenModel screen,
                               const bool software_content,
                               const bool session_state,
                               const bool cpu_buffer_synchronization)
    : transport_(std::move(path), GWIPC_ROLE_COMPOSITOR,
                 (software_content ? kContentCapabilities
                                   : kMetadataCapabilities) |
                     (session_state ? GWIPC_CAP_SESSION_STATE : 0) |
                     (cpu_buffer_synchronization
                          ? GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION
                          : 0),
                 "glasswyrmd-compositor"),
      screen_(screen), software_content_(software_content),
      session_state_(session_state) {}

bool CompositorPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

std::vector<CompositorBufferRelease> CompositorPeer::take_releases() {
  auto result = std::move(releases_);
  releases_.clear();
  return result;
}

std::vector<CompositorSessionStateChange>
CompositorPeer::take_session_state_changes() {
  auto result = std::move(session_state_changes_);
  session_state_changes_.clear();
  return result;
}

bool CompositorPeer::acknowledge_session_state(
    const CompositorSessionStateChange& request,
    const gwipc_session_state_result result, std::string& error) {
  if (!session_state_ || !transport_.established() || request.sequence == 0) {
    error = "compositor session state reply is unavailable";
    return false;
  }
  gwipc_session_state_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.generation = request.change.generation;
  acknowledged.state = request.change.state;
  acknowledged.result = result;
  if (!enqueue_contract(transport_.connection(),
                        GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED,
                        GWIPC_FLAG_REPLY, acknowledged,
                        gwipc_contract_encode_session_state_acknowledged,
                        nullptr, request.sequence)) {
    error = "could not queue compositor session state acknowledgement";
    return false;
  }
  error.clear();
  return true;
}

PeerProcessOutcome CompositorPeer::drain(std::string &error) {
  for (;;) {
    glasswyrm::ipc::Message message;
    const auto status = transport_.handle().receive(message);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return PeerProcessOutcome::Progress;
    if (status != GWIPC_STATUS_OK) {
      error = "compositor peer receive failed";
      return PeerProcessOutcome::Fatal;
    }
    if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_SESSION_STATE_CHANGE &&
        session_state_) {
      gwipc_decoded_contract* raw_change = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw_change) !=
          GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded_change(
          raw_change);
      const auto* change =
          gwipc_decoded_session_state_change(decoded_change.get());
      if (!change || change->generation == 0) {
        error = "invalid compositor session state change";
        return PeerProcessOutcome::Fatal;
      }
      session_state_changes_.push_back(
          {*change, gwipc_message_sequence(message.get())});
      continue;
    }
    if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_BUFFER_RELEASE &&
        software_content_) {
      gwipc_decoded_contract* raw_release = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw_release) !=
          GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded_release(
          raw_release);
      const auto* release =
          gwipc_decoded_buffer_release(decoded_release.get());
      if (!release || release->buffer_id == 0) {
        error = "invalid compositor buffer release";
        return PeerProcessOutcome::Fatal;
      }
      releases_.push_back({release->buffer_id, release->reason});
      continue;
    }
    if (gwipc_message_type(message.get()) != GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
      error = gwipc_message_type(message.get()) == GWIPC_MESSAGE_BUFFER_RELEASE
                  ? "metadata-only compositor sent a buffer release"
                  : "unexpected compositor bootstrap message";
      return PeerProcessOutcome::Fatal;
    }
    gwipc_decoded_contract *raw = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
      return PeerProcessOutcome::Fatal;
    std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded(raw);
    const auto *ack = gwipc_decoded_frame_acknowledged(decoded.get());
    const auto expected_commit = content_submission_ ? pending_content_.commit_id
                                                     : pending_.commit_id;
    const auto expected_generation =
        content_submission_ ? pending_content_.generation : pending_.generation;
    if (ack && ack->commit_id == expected_commit && ack->output_id == 1 &&
        ack->presented_generation == expected_generation &&
        ack->result >= GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA &&
        ack->result <= GWIPC_FRAME_DROPPED &&
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) != 0 &&
        gwipc_message_reply_to(message.get()) == commit_sequence_) {
      state_ = PeerBootstrapState::Synchronized;
      return PeerProcessOutcome::SemanticRejected;
    }
    if (!ack || ack->commit_id != expected_commit || ack->output_id != 1 ||
        ack->presented_generation != expected_generation ||
        ack->result != GWIPC_FRAME_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) != commit_sequence_) {
      error = "invalid compositor bootstrap acknowledgement";
      return PeerProcessOutcome::Fatal;
    }
    if (!content_submission_)
      compositor_buffer_replay::promote(pending_, replay_input_);
    content_submission_ = false;
    state_ = PeerBootstrapState::Synchronized;
  }
}

PeerProcessOutcome CompositorPeer::process(const short revents, std::string &error) {
  if (!transport_.process(revents, error)) {
    state_ = PeerBootstrapState::Failed;
    return PeerProcessOutcome::Disconnected;
  }
  if (state_ == PeerBootstrapState::Connecting && transport_.established() &&
      !send_bootstrap(error)) {
    state_ = PeerBootstrapState::Failed;
    return PeerProcessOutcome::Fatal;
  }
  if (state_ == PeerBootstrapState::AwaitingReply) {
    const auto outcome = drain(error);
    if (outcome == PeerProcessOutcome::Fatal)
      state_ = PeerBootstrapState::Failed;
    return outcome;
  }
  if (state_ == PeerBootstrapState::Synchronized &&
      (software_content_ || session_state_))
    return drain(error);
  return PeerProcessOutcome::Progress;
}

void CompositorPeer::forget_cursor_replay() noexcept {
  forget_cursor(replay_input_);
  forget_cursor(pending_);
}

void CompositorPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  commit_sequence_ = 0;
  content_submission_ = false;
  releases_.clear();
  session_state_changes_.clear();
}
} // namespace glasswyrm::server
