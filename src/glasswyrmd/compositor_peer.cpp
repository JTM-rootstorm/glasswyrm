#include "glasswyrmd/compositor_peer.hpp"

#include <algorithm>
#include <memory>
#include <set>

namespace glasswyrm::server {
namespace {
constexpr gwipc_capabilities kCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT |
    GWIPC_CAP_WINDOW_LIFECYCLE;
struct ContractDelete {
  void operator()(gwipc_contract_payload *p) const {
    gwipc_contract_payload_destroy(p);
  }
};
struct ControlDelete {
  void operator()(gwipc_control_payload *p) const {
    gwipc_control_payload_destroy(p);
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
                      std::uint64_t *out_sequence = nullptr) {
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
  if (out_sequence)
    return gwipc_connection_enqueue_with_sequence(
               connection, &message, out_sequence) == GWIPC_STATUS_OK;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
template <class T, class Encoder>
bool enqueue_control(gwipc_connection *connection, std::uint16_t type,
                     const T &value, Encoder encoder) {
  gwipc_control_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_control_payload, ControlDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
} // namespace

CompositorPeer::CompositorPeer(std::string path,
                               const gw::protocol::x11::ScreenModel screen)
    : transport_(std::move(path), GWIPC_ROLE_COMPOSITOR, kCapabilities,
                 "glasswyrmd-compositor"),
      screen_(screen) {}

bool CompositorPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

bool CompositorPeer::send_bootstrap(std::string &error) {
  return submit(replay_input_.commit_id != 0
                    ? replay_input_
                    : CompositorSnapshotSubmission{1, 1, {}, {}},
                error);
}

bool CompositorPeer::submit(const CompositorSnapshotSubmission &submission,
                            std::string &error) {
  if (!transport_.established() ||
      (state_ != PeerBootstrapState::Connecting &&
       state_ != PeerBootstrapState::Synchronized) ||
      submission.commit_id == 0 || submission.generation == 0 ||
      submission.surfaces.size() != submission.policies.size()) {
    error = "compositor peer is not ready for a complete metadata snapshot";
    return false;
  }
  std::set<std::uint64_t> surface_ids;
  std::set<std::uint64_t> policy_ids;
  for (const auto &surface : submission.surfaces)
    if (surface.presentation_flags !=
            GWIPC_SURFACE_PRESENTATION_METADATA_ONLY ||
        surface.x11_window_id == 0 ||
        !surface_ids.insert(surface.surface_id).second) {
      error = "invalid metadata-only surface submission";
      return false;
    }
  for (const auto &policy : submission.policies)
    if (policy.surface_id == 0 || policy.x11_window_id == 0 ||
        !policy_ids.insert(policy.surface_id).second) {
      error = "invalid surface policy submission";
      return false;
    }
  if (surface_ids != policy_ids) {
    error = "surface and policy IDs do not match";
    return false;
  }
  for (const auto &surface : submission.surfaces) {
    const auto policy =
        std::find_if(submission.policies.begin(), submission.policies.end(),
                     [&](const auto &item) {
                       return item.surface_id == surface.surface_id;
                     });
    if (policy == submission.policies.end() ||
        policy->x11_window_id != surface.x11_window_id) {
      error = "surface and policy X11 IDs do not match";
      return false;
    }
  }
  auto *connection = transport_.connection();
  const auto count = static_cast<std::uint32_t>(1 + submission.surfaces.size() +
                                                submission.policies.size());
  gwipc_snapshot_begin begin{sizeof(begin),
                             submission.commit_id,
                             GWIPC_SNAPSHOT_COMPLETE_SESSION,
                             0,
                             submission.generation,
                             count,
                             {}};
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = screen_.width_pixels;
  output.logical_height = screen_.height_pixels;
  output.physical_pixel_width = screen_.width_pixels;
  output.physical_pixel_height = screen_.height_pixels;
  output.refresh_millihertz = 60000;
  output.scale_numerator = 1;
  output.scale_denominator = 1;
  output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  output.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  output.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  gwipc_snapshot_end end{
      sizeof(end), submission.commit_id, submission.generation, count, {}};
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = submission.commit_id;
  commit.output_id = 1;
  commit.producer_generation = submission.generation;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, output,
                        gwipc_contract_encode_output_upsert)) {
    error = "could not queue compositor snapshot header";
    return false;
  }
  for (const auto &surface : submission.surfaces) {
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, surface,
                          gwipc_contract_encode_surface_upsert)) {
      error = "invalid metadata-only surface submission";
      return false;
    }
  }
  for (const auto &policy : submission.policies) {
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_POLICY_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, policy,
                          gwipc_contract_encode_surface_policy_upsert)) {
      error = "could not queue surface policy submission";
      return false;
    }
  }
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end) ||
      !enqueue_contract(
          connection, GWIPC_MESSAGE_FRAME_COMMIT, GWIPC_FLAG_ACK_REQUIRED,
          commit, gwipc_contract_encode_frame_commit, &commit_sequence_)) {
    error = "could not queue compositor bootstrap";
    return false;
  }
  pending_ = submission;
  state_ = PeerBootstrapState::AwaitingReply;
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
    if (ack && ack->commit_id == pending_.commit_id && ack->output_id == 1 &&
        ack->presented_generation == pending_.generation &&
        ack->result >= GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA &&
        ack->result <= GWIPC_FRAME_DROPPED &&
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) != 0 &&
        gwipc_message_reply_to(message.get()) == commit_sequence_) {
      state_ = PeerBootstrapState::Synchronized;
      return PeerProcessOutcome::SemanticRejected;
    }
    if (!ack || ack->commit_id != pending_.commit_id || ack->output_id != 1 ||
        ack->presented_generation != pending_.generation ||
        ack->result != GWIPC_FRAME_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) != commit_sequence_) {
      error = "invalid compositor bootstrap acknowledgement";
      return PeerProcessOutcome::Fatal;
    }
    replay_input_ = pending_;
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
  return PeerProcessOutcome::Progress;
}

void CompositorPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  commit_sequence_ = 0;
}
} // namespace glasswyrm::server
