#include "glasswyrmd/compositor_peer.hpp"

#include <algorithm>
#include <map>
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
    GWIPC_CAP_DAMAGE_REGIONS;
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
bool enqueue_buffer(gwipc_connection* connection,
                    const CompositorSnapshotSubmission::Buffer& buffer) {
  gwipc_contract_payload* raw = nullptr;
  if (buffer.fd < 0 ||
      gwipc_contract_encode_buffer_attach(&buffer.attach, &raw) !=
          GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = GWIPC_MESSAGE_BUFFER_ATTACH;
  message.flags = GWIPC_FLAG_SNAPSHOT_ITEM;
  message.payload = data;
  message.payload_size = size;
  message.fds = &buffer.fd;
  message.fd_count = 1;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
} // namespace

CompositorPeer::CompositorPeer(std::string path,
                               const gw::protocol::x11::ScreenModel screen,
                               const bool software_content,
                               const bool session_state)
    : transport_(std::move(path), GWIPC_ROLE_COMPOSITOR,
                 (software_content ? kContentCapabilities
                                   : kMetadataCapabilities) |
                     (session_state ? GWIPC_CAP_SESSION_STATE : 0),
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

bool CompositorPeer::send_bootstrap(std::string &error) {
  return submit(replay_input_.commit_id != 0
                    ? replay_input_
                    : CompositorSnapshotSubmission{1, 1, {}, {}, {}, {}},
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
            (software_content_ ? 0U
                               : GWIPC_SURFACE_PRESENTATION_METADATA_ONLY) ||
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
  if (!software_content_ &&
      (!submission.buffers.empty() || !submission.damages.empty())) {
    error = "metadata-only compositor submission contains pixel content";
    return false;
  }
  std::set<std::uint64_t> buffer_ids;
  for (const auto& buffer : submission.buffers) {
    if (buffer.fd < 0 || buffer.attach.buffer_id == 0 ||
        !buffer_ids.insert(buffer.attach.buffer_id).second ||
        !surface_ids.contains(buffer.attach.surface_id)) {
      error = "invalid buffered compositor attachment";
      return false;
    }
  }
  for (const auto& damage : submission.damages)
    if (!surface_ids.contains(damage.surface_id) || damage.rectangles.empty()) {
      error = "invalid buffered compositor damage";
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
  const auto count = static_cast<std::uint32_t>(
      1 + submission.surfaces.size() + submission.policies.size() +
      submission.buffers.size() + submission.damages.size());
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
  for (const auto& buffer : submission.buffers) {
    if (!enqueue_buffer(connection, buffer)) {
      error = "could not queue buffered compositor attachment";
      return false;
    }
  }
  for (const auto& damage : submission.damages) {
    gwipc_surface_damage value{};
    value.struct_size = sizeof(value);
    value.surface_id = damage.surface_id;
    value.rectangle_count =
        static_cast<std::uint32_t>(damage.rectangles.size());
    value.rectangles = damage.rectangles.data();
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_DAMAGE,
                          GWIPC_FLAG_SNAPSHOT_ITEM, value,
                          gwipc_contract_encode_surface_damage)) {
      error = "could not queue buffered compositor damage";
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
  content_submission_ = false;
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

bool CompositorPeer::submit_content(
    const CompositorContentSubmission& submission, std::string& error) {
  if (!software_content_ || !transport_.established() ||
      state_ != PeerBootstrapState::Synchronized || submission.commit_id == 0 ||
      submission.generation == 0 || submission.damages.empty()) {
    error = "compositor peer is not ready for incremental content";
    return false;
  }
  auto* connection = transport_.connection();
  for (const auto& damage : submission.damages) {
    gwipc_surface_damage value{};
    value.struct_size = sizeof(value);
    value.surface_id = damage.surface_id;
    value.rectangle_count =
        static_cast<std::uint32_t>(damage.rectangles.size());
    value.rectangles = damage.rectangles.data();
    if (damage.surface_id == 0 || damage.rectangles.empty() ||
        !enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_DAMAGE, 0, value,
                          gwipc_contract_encode_surface_damage)) {
      error = "could not queue incremental compositor damage";
      return false;
    }
  }
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = submission.commit_id;
  commit.output_id = 1;
  commit.producer_generation = submission.generation;
  if (!enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                        GWIPC_FLAG_ACK_REQUIRED, commit,
                        gwipc_contract_encode_frame_commit,
                        &commit_sequence_)) {
    error = "could not queue incremental compositor frame";
    return false;
  }
  pending_content_ = submission;
  content_submission_ = true;
  state_ = PeerBootstrapState::AwaitingReply;
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

void CompositorPeer::promote_replay_snapshot() {
  CompositorSnapshotSubmission replay = pending_;
  replay.buffers.clear();
  std::map<std::uint64_t, CompositorSnapshotSubmission::Buffer> attachments;
  const std::set<std::uint64_t> retained_surfaces = [&] {
    std::set<std::uint64_t> ids;
    for (const auto& surface : pending_.surfaces) ids.insert(surface.surface_id);
    return ids;
  }();
  for (const auto& buffer : replay_input_.buffers)
    if (retained_surfaces.contains(buffer.attach.surface_id))
      attachments[buffer.attach.surface_id] = buffer;
  for (const auto& buffer : pending_.buffers)
    attachments[buffer.attach.surface_id] = buffer;
  replay.buffers.reserve(attachments.size());
  for (const auto& surface : pending_.surfaces) {
    const auto found = attachments.find(surface.surface_id);
    if (found != attachments.end()) replay.buffers.push_back(found->second);
  }
  replay_input_ = std::move(replay);
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
    if (!content_submission_) promote_replay_snapshot();
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

void CompositorPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  commit_sequence_ = 0;
  content_submission_ = false;
  releases_.clear();
  session_state_changes_.clear();
}
} // namespace glasswyrm::server
