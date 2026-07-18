#include "glasswyrmd/compositor_peer.hpp"

#include "glasswyrmd/compositor_buffer_replay.hpp"

#include <algorithm>
#include <memory>
#include <set>

namespace glasswyrm::server {
namespace {
struct ContractDelete {
  void operator()(gwipc_contract_payload* payload) const {
    gwipc_contract_payload_destroy(payload);
  }
};
struct ControlDelete {
  void operator()(gwipc_control_payload* payload) const {
    gwipc_control_payload_destroy(payload);
  }
};

template <class T, class Encoder>
bool enqueue_contract(gwipc_connection* connection, const std::uint16_t type,
                      const std::uint32_t flags, const T& value,
                      Encoder encoder, std::uint64_t* out_sequence = nullptr) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload.get(), &size);
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
bool encodable_contract(const T& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  const auto status = encoder(&value, &raw);
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  return status == GWIPC_STATUS_OK;
}

template <class T, class Encoder>
bool enqueue_control(gwipc_connection* connection, const std::uint16_t type,
                     const T& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  std::unique_ptr<gwipc_control_payload, ControlDelete> payload(raw);
  std::size_t size = 0;
  const auto* data = gwipc_control_payload_data(payload.get(), &size);
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
  const int fds[2] = {buffer.fd, buffer.synchronization_fd};
  message.fds = fds;
  message.fd_count =
      buffer.attach.synchronization == GWIPC_SYNCHRONIZATION_EVENTFD ? 2 : 1;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
}  // namespace

bool CompositorPeer::send_bootstrap(std::string& error) {
  if (replay_input_.commit_id != 0 &&
      !compositor_buffer_replay::prepare(replay_input_, error))
    return false;
  return submit(replay_input_.commit_id != 0
                    ? replay_input_
                    : CompositorSnapshotSubmission{1, 1, {}, {}, {}, {}},
                error);
}

void CompositorPeer::retain_cursor_records(
    CompositorSnapshotSubmission& submission) const {
  const auto is_cursor = [](const auto& surface) {
    return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  };
  if (std::ranges::any_of(submission.surfaces, is_cursor)) return;
  const auto retained = std::ranges::find_if(replay_input_.surfaces, is_cursor);
  if (retained != replay_input_.surfaces.end()) {
    submission.surfaces.push_back(*retained);
    const auto membership = std::ranges::find_if(
        replay_input_.surface_outputs, [&](const auto& state) {
          return state.state.surface_id == retained->surface_id;
        });
    if (membership != replay_input_.surface_outputs.end())
      submission.surface_outputs.push_back(*membership);
  }
}

bool CompositorPeer::validate_surface_membership_records(
    const CompositorSnapshotSubmission& submission, std::string& error) const {
  std::set<std::uint64_t> surface_ids;
  std::set<std::uint64_t> normal_surface_ids;
  std::set<std::uint64_t> policy_ids;
  std::set<std::uint64_t> expected_membership_ids;
  std::size_t cursor_count = 0;
  for (const auto& surface : submission.surfaces) {
    const bool cursor =
        surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
    const bool valid_cursor =
        cursor && software_content_ && surface.x11_window_id == 0 &&
        surface.parent_surface_id == 0 && surface.output_id != 0 &&
        surface.logical_width > 0 && surface.logical_width <= 64 &&
        surface.logical_height > 0 && surface.logical_height <= 64 &&
        surface.transform == GWIPC_TRANSFORM_NORMAL &&
        surface.opacity == GWIPC_OPACITY_ONE &&
        surface.scale_numerator >= 1 && surface.scale_numerator <= 4 &&
        surface.scale_denominator == 1 &&
        surface.fullscreen_eligible == GWIPC_TRI_STATE_UNKNOWN &&
        surface.direct_scanout_eligible == GWIPC_TRI_STATE_UNKNOWN;
    const bool valid_normal =
        !cursor &&
        surface.presentation_flags ==
            (software_content_ ? 0U
                               : GWIPC_SURFACE_PRESENTATION_METADATA_ONLY) &&
        surface.x11_window_id != 0;
    if ((!valid_cursor && !valid_normal) ||
        !surface_ids.insert(surface.surface_id).second ||
        (cursor && ++cursor_count > 1)) {
      error = "invalid metadata-only surface submission";
      return false;
    }
    if (!cursor)
      normal_surface_ids.insert(surface.surface_id);
    if (surface.presentation_flags !=
        GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
      expected_membership_ids.insert(surface.surface_id);
  }
  for (const auto& policy : submission.policies)
    if (policy.surface_id == 0 || policy.x11_window_id == 0 ||
        !policy_ids.insert(policy.surface_id).second) {
      error = "invalid surface policy submission";
      return false;
    }
  if (normal_surface_ids != policy_ids) {
    error = "surface and policy IDs do not match";
    return false;
  }
  std::set<std::uint64_t> membership_ids;
  for (const auto& membership : submission.surface_outputs) {
    const auto& state = membership.state;
    auto encoded_state = state;
    encoded_state.output_ids = membership.output_ids.data();
    encoded_state.output_count = membership.output_ids.size();
    if (state.struct_size < sizeof(state) || state.surface_id == 0 ||
        !membership_ids.insert(state.surface_id).second ||
        !encodable_contract(encoded_state,
                            gwipc_contract_encode_surface_output_state)) {
      error = "invalid surface output membership submission";
      return false;
    }
  }
  if (output_model_ ? membership_ids != expected_membership_ids
                    : !membership_ids.empty()) {
    error = "surface output membership IDs do not match surfaces";
    return false;
  }
  return true;
}

bool CompositorPeer::validate_buffer_damage_records(
    const CompositorSnapshotSubmission& submission, std::string& error) const {
  if (!software_content_ &&
      (!submission.buffers.empty() || !submission.damages.empty())) {
    error = "metadata-only compositor submission contains pixel content";
    return false;
  }
  std::set<std::uint64_t> surface_ids;
  for (const auto& surface : submission.surfaces)
    surface_ids.insert(surface.surface_id);
  std::set<std::uint64_t> buffer_ids;
  for (const auto& buffer : submission.buffers) {
    const bool synchronized =
        buffer.attach.synchronization == GWIPC_SYNCHRONIZATION_EVENTFD;
    if (buffer.fd < 0 || synchronized != (buffer.synchronization_fd >= 0) ||
        buffer.attach.buffer_id == 0 ||
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
  return true;
}

bool CompositorPeer::validate_surface_policy_links(
    const CompositorSnapshotSubmission& submission, std::string& error) const {
  for (const auto& surface : submission.surfaces) {
    if (surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR)
      continue;
    const auto policy = std::ranges::find_if(
        submission.policies, [&](const auto& item) {
          return item.surface_id == surface.surface_id;
        });
    if (policy == submission.policies.end() ||
        policy->x11_window_id != surface.x11_window_id) {
      error = "surface and policy X11 IDs do not match";
      return false;
    }
  }
  return true;
}

bool CompositorPeer::enqueue_output_records(
    const CompositorSnapshotSubmission& submission,
    const std::uint32_t item_count, std::string& error) {
  gwipc_snapshot_begin begin{sizeof(begin),
                             submission.commit_id,
                             GWIPC_SNAPSHOT_COMPLETE_SESSION,
                             0,
                             submission.generation,
                             item_count,
                             {}};
  auto* connection = transport_.connection();
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin)) {
    error = "could not queue compositor snapshot header";
    return false;
  }
  if (output_model_) {
    for (const auto& output : submission.outputs)
      if (!enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                            GWIPC_FLAG_SNAPSHOT_ITEM, output,
                            gwipc_contract_encode_output_upsert)) {
        error = "could not queue compositor output map";
        return false;
      }
  } else {
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
    if (!enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, output,
                          gwipc_contract_encode_output_upsert)) {
      error = "could not queue compositor snapshot header";
      return false;
    }
  }
  return true;
}

bool CompositorPeer::enqueue_surface_membership_records(
    const CompositorSnapshotSubmission& submission, std::string& error) {
  auto* connection = transport_.connection();
  for (const auto& surface : submission.surfaces)
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, surface,
                          gwipc_contract_encode_surface_upsert)) {
      error = "invalid metadata-only surface submission";
      return false;
    }
  for (const auto& policy : submission.policies)
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_POLICY_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, policy,
                          gwipc_contract_encode_surface_policy_upsert)) {
      error = "could not queue surface policy submission";
      return false;
    }
  for (const auto& membership : submission.surface_outputs) {
    auto state = membership.state;
    state.output_ids = membership.output_ids.data();
    state.output_count = membership.output_ids.size();
    if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_OUTPUT_STATE,
                          GWIPC_FLAG_SNAPSHOT_ITEM, state,
                          gwipc_contract_encode_surface_output_state)) {
      error = "could not queue surface output membership";
      return false;
    }
  }
  return true;
}

bool CompositorPeer::enqueue_buffer_damage_records(
    const CompositorSnapshotSubmission& submission, std::string& error) {
  auto* connection = transport_.connection();
  for (const auto& buffer : submission.buffers)
    if (!enqueue_buffer(connection, buffer)) {
      error = "could not queue buffered compositor attachment";
      return false;
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
  return true;
}

bool CompositorPeer::enqueue_snapshot_completion_records(
    const CompositorSnapshotSubmission& submission,
    const std::uint32_t item_count, std::string& error) {
  gwipc_snapshot_end end{sizeof(end), submission.commit_id,
                         submission.generation, item_count, {}};
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = submission.commit_id;
  commit.output_id = output_model_ ? 0 : 1;
  commit.producer_generation = submission.generation;
  auto* connection = transport_.connection();
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                        GWIPC_FLAG_ACK_REQUIRED, commit,
                        gwipc_contract_encode_frame_commit,
                        &commit_sequence_)) {
    error = "could not queue compositor bootstrap";
    return false;
  }
  return true;
}

bool CompositorPeer::submit(const CompositorSnapshotSubmission& submission,
                            std::string& error) {
  auto complete = submission;
  retain_cursor_records(complete);
  if (!transport_.established() ||
      (state_ != PeerBootstrapState::Connecting &&
       state_ != PeerBootstrapState::Synchronized) ||
      complete.commit_id == 0 || complete.generation == 0) {
    error = "compositor peer is not ready for a complete metadata snapshot";
    return false;
  }
  if (output_model_) {
    if (!output_layout_ || complete.outputs.empty() ||
        complete.outputs.size() != output_layout_->descriptors.size()) {
      error = "output-model snapshot does not contain the complete output map";
      return false;
    }
    std::set<std::uint64_t> output_ids;
    for (const auto& output_record : complete.outputs)
      if (output_record.struct_size < sizeof(output_record) ||
          output_record.output_id == 0 ||
          !output_layout_->descriptors.contains(
              glasswyrm::output::OutputId{output_record.output_id}) ||
          !encodable_contract(output_record,
                              gwipc_contract_encode_output_upsert) ||
          !output_ids.insert(output_record.output_id).second) {
        error = "output-model snapshot contains an invalid output map";
        return false;
      }
  } else if (!complete.outputs.empty() || !complete.surface_outputs.empty()) {
    error = "historical compositor snapshot contains output-model records";
    return false;
  }
  if (!validate_surface_membership_records(complete, error) ||
      !validate_buffer_damage_records(complete, error) ||
      !compositor_buffer_replay::rearm_snapshot(complete, replay_input_,
                                                error) ||
      !validate_surface_policy_links(complete, error))
    return false;
  const auto output_count = output_model_ ? complete.outputs.size() : 1U;
  const auto item_count = static_cast<std::uint32_t>(
      output_count + complete.surfaces.size() + complete.policies.size() +
      complete.surface_outputs.size() +
      complete.buffers.size() + complete.damages.size());
  if (!enqueue_output_records(complete, item_count, error) ||
      !enqueue_surface_membership_records(complete, error) ||
      !enqueue_buffer_damage_records(complete, error) ||
      !enqueue_snapshot_completion_records(complete, item_count, error))
    return false;
  for (auto& membership : complete.surface_outputs) {
    membership.state.output_ids = nullptr;
    membership.state.output_count = membership.output_ids.size();
  }
  pending_ = std::move(complete);
  content_submission_ = false;
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

bool CompositorPeer::submit_cursor(
    const CompositorCursorSubmission& submission,
    const std::uint64_t commit_id, const std::uint64_t generation,
    std::string& error) {
  if (!software_content_ || commit_id == 0 || generation == 0 ||
      submission.surface.presentation_flags !=
          GWIPC_SURFACE_PRESENTATION_CURSOR) {
    error = "invalid cursor compositor submission";
    return false;
  }
  auto complete = replay_input_;
  complete.commit_id = commit_id;
  complete.generation = generation;
  std::erase_if(complete.surfaces, [](const auto& surface) {
    return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  });
  complete.surfaces.push_back(submission.surface);
  complete.buffers.clear();
  complete.damages.clear();
  if (submission.buffer) complete.buffers.push_back(*submission.buffer);
  if (submission.damage) complete.damages.push_back(*submission.damage);
  return submit(complete, error);
}

bool CompositorPeer::submit_content(
    const CompositorContentSubmission& submission, std::string& error) {
  if (!software_content_ || !transport_.established() ||
      state_ != PeerBootstrapState::Synchronized || submission.commit_id == 0 ||
      submission.generation == 0 || submission.damages.empty()) {
    error = "compositor peer is not ready for incremental content";
    return false;
  }
  if (!compositor_buffer_replay::rearm_content(submission.damages,
                                               replay_input_, error))
    return false;
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
  commit.output_id = output_model_ ? 0 : 1;
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

}  // namespace glasswyrm::server
