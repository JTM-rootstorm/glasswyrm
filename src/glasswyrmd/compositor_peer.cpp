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
constexpr gwipc_capabilities kOutputModelCapabilities =
    GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
    GWIPC_CAP_SCALE_METADATA;
constexpr gwipc_capabilities kVrrCapabilities =
    GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY |
    GWIPC_CAP_PRESENTATION_TIMING;
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
  std::erase_if(submission.surface_outputs, [&](const auto& state) {
    return cursor_surfaces.contains(state.state.surface_id);
  });
}

bool same_mode_identity(const output::OutputMode &left,
                        const output::OutputMode &right) noexcept {
  return left.id == right.id && left.output_id == right.output_id &&
         left.physical_width == right.physical_width &&
         left.physical_height == right.physical_height &&
         left.refresh_millihertz == right.refresh_millihertz &&
         left.flags == right.flags && left.name == right.name &&
         left.preferred == right.preferred;
}

bool same_descriptor_identity(const output::OutputDescriptor &left,
                              const output::OutputDescriptor &right) noexcept {
  if (left.id != right.id || left.name != right.name ||
      left.kind != right.kind || left.connected != right.connected ||
      left.physical_width_mm != right.physical_width_mm ||
      left.physical_height_mm != right.physical_height_mm ||
      left.supported_transform_mask != right.supported_transform_mask ||
      left.minimum_scale != right.minimum_scale ||
      left.maximum_scale != right.maximum_scale ||
      left.maximum_scale_denominator != right.maximum_scale_denominator ||
      left.mode_configurable != right.mode_configurable ||
      left.scale_configurable != right.scale_configurable ||
      left.transform_configurable != right.transform_configurable ||
      left.primary_eligible != right.primary_eligible ||
      left.arbitrary_headless_mode != right.arbitrary_headless_mode ||
      left.maximum_physical_width != right.maximum_physical_width ||
      left.maximum_physical_height != right.maximum_physical_height ||
      left.maximum_physical_pixels != right.maximum_physical_pixels ||
      left.modes.size() != right.modes.size())
    return false;
  return std::ranges::equal(left.modes, right.modes, same_mode_identity);
}

bool same_inventory_identity(const output::OutputLayout &left,
                             const output::OutputLayout &right) noexcept {
  if (left.descriptors.size() != right.descriptors.size()) return false;
  for (const auto &[id, descriptor] : left.descriptors) {
    const auto found = right.descriptors.find(id);
    if (found == right.descriptors.end() ||
        !same_descriptor_identity(descriptor, found->second))
      return false;
  }
  return true;
}

bool same_vrr_capability(
    const gwipc_output_vrr_capability_upsert& left,
    const gwipc_output_vrr_capability_upsert& right) noexcept {
  return left.output_id == right.output_id &&
         left.connector_property_present == right.connector_property_present &&
         left.hardware_capable == right.hardware_capable &&
         left.kms_controllable == right.kms_controllable &&
         left.simulated == right.simulated &&
         left.range_available == right.range_available &&
         left.atomic_required == right.atomic_required &&
         left.minimum_refresh_millihertz == right.minimum_refresh_millihertz &&
         left.maximum_refresh_millihertz == right.maximum_refresh_millihertz &&
         left.reason_flags == right.reason_flags && left.flags == right.flags;
}

} // namespace

CompositorPeer::CompositorPeer(std::string path,
                               const gw::protocol::x11::ScreenModel screen,
                               const bool software_content,
                               const bool session_state,
                               const bool cpu_buffer_synchronization,
                               const bool output_model,
                               const bool vrr_profile)
    : transport_(std::move(path), GWIPC_ROLE_COMPOSITOR,
                 (software_content ? kContentCapabilities
                                   : kMetadataCapabilities) |
                     (session_state ? GWIPC_CAP_SESSION_STATE : 0) |
                     (cpu_buffer_synchronization
                          ? GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION
                          : 0) |
                     (output_model ? kOutputModelCapabilities : 0) |
                     (vrr_profile ? kVrrCapabilities : 0),
                 "glasswyrmd-compositor"),
      screen_(screen), software_content_(software_content),
      session_state_(session_state), output_model_(output_model),
      vrr_profile_(vrr_profile) {}

bool CompositorPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

bool CompositorPeer::can_adopt_output_layout(
    const output::OutputLayout& layout) const noexcept {
  if (!output_model_ || !output_layout_ || !output::validate_layout(layout) ||
      output_layout_->descriptors.size() != layout.descriptors.size())
    return false;
  for (const auto &[id, descriptor] : output_layout_->descriptors) {
    const auto found = layout.descriptors.find(id);
    if (found == layout.descriptors.end() ||
        found->second.name != descriptor.name ||
        found->second.kind != descriptor.kind)
      return false;
  }
  return true;
}

bool CompositorPeer::adopt_output_layout(output::OutputLayout layout) {
  if (!can_adopt_output_layout(layout))
    return false;
  output_layout_ = std::move(layout);
  return true;
}

bool CompositorPeer::begin_output_inventory(std::string &error) {
  if (next_output_query_id_ == 0) {
    error = "compositor output inventory query identity was exhausted";
    return false;
  }
  pending_output_inventory_ =
      std::make_unique<CompositorOutputInventory>(vrr_profile_);
  const auto query_id = next_output_query_id_++;
  if (!pending_output_inventory_->start(transport_.connection(), query_id,
                                        error)) {
    pending_output_inventory_.reset();
    return false;
  }
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

bool CompositorPeer::accept_output_inventory(std::string &error) {
  const auto *layout = pending_output_inventory_
                           ? pending_output_inventory_->layout()
                           : nullptr;
  if (layout == nullptr) {
    error = "compositor output inventory completed without a layout";
    return false;
  }
  if (reference_output_inventory_ &&
      !same_inventory_identity(*reference_output_inventory_, *layout)) {
    error = "compositor output inventory changed across reconnect";
    return false;
  }
  if (!reference_output_inventory_) reference_output_inventory_ = *layout;
  // A reconnect inventory proves that the compositor still exposes the same
  // immutable outputs; it does not supersede an accepted server-side layout.
  // Preserve the committed configuration so bootstrap replays it into the
  // fresh compositor instead of silently reverting to backend defaults.
  if (!output_layout_) output_layout_ = *layout;
  if (vrr_profile_) {
    const auto& capabilities = pending_output_inventory_->vrr_capabilities();
    const auto& policies = pending_output_inventory_->vrr_policies();
    const auto& states = pending_output_inventory_->vrr_states();
    const auto& timings = pending_output_inventory_->timings();
    if (vrr_cache_.outputs().empty()) {
      if (!vrr_cache_.replace_inventory(capabilities, policies) ||
          !vrr_cache_.seed_compositor_state(states, timings)) {
        error = "compositor output inventory contained an invalid M14 VRR state";
        return false;
      }
    } else {
      VrrStateCache validated;
      if (!validated.replace_inventory(capabilities, policies) ||
          !validated.seed_compositor_state(states, timings) ||
          validated.outputs().size() != vrr_cache_.outputs().size()) {
        error = "reconnected compositor returned an invalid M14 VRR inventory";
        return false;
      }
      bool policies_match = true;
      for (const auto& [output_id, incoming] : validated.outputs()) {
        const auto current = vrr_cache_.outputs().find(output_id);
        if (current == vrr_cache_.outputs().end() ||
            !same_vrr_capability(current->second.capability,
                                 incoming.capability)) {
          error = "compositor VRR inventory changed across reconnect";
          return false;
        }
        policies_match = policies_match &&
                         current->second.policy.mode == incoming.policy.mode;
      }
      if (policies_match &&
          !vrr_cache_.seed_compositor_state(states, timings)) {
        error = "reconnected compositor VRR state could not be restored";
        return false;
      }
    }
    if (!accepted_vrr_cache_) accepted_vrr_cache_ = vrr_cache_;
  }
  pending_output_inventory_.reset();
  state_ = PeerBootstrapState::Synchronized;
  error.clear();
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
    if (pending_output_inventory_) {
      if (!pending_output_inventory_->consume(message.get(), error))
        return PeerProcessOutcome::Fatal;
      if (pending_output_inventory_->state() ==
          CompositorInventoryState::Complete) {
        if (!accept_output_inventory(error)) return PeerProcessOutcome::Fatal;
        return PeerProcessOutcome::Progress;
      }
      continue;
    }
    const auto type = gwipc_message_type(message.get());
    if (vrr_profile_ && type == GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT) {
      if (frame_acknowledged_) {
        error = "compositor VRR state arrived after the frame acknowledgement";
        return PeerProcessOutcome::Fatal;
      }
      gwipc_decoded_contract* raw_state = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw_state) !=
          GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded_state(
          raw_state);
      const auto* state =
          gwipc_decoded_output_vrr_state_upsert(decoded_state.get());
      if (!state) {
        error = "invalid compositor VRR output state";
        return PeerProcessOutcome::Fatal;
      }
      vrr_response_.output_states.push_back(*state);
      continue;
    }
    if (vrr_profile_ && type == GWIPC_MESSAGE_PRESENTATION_TIMING) {
      if (frame_acknowledged_) {
        error = "compositor presentation timing arrived after the frame acknowledgement";
        return PeerProcessOutcome::Fatal;
      }
      gwipc_decoded_contract* raw_timing = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw_timing) !=
          GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded_timing(
          raw_timing);
      const auto* timing =
          gwipc_decoded_presentation_timing(decoded_timing.get());
      if (!timing) {
        error = "invalid compositor presentation timing";
        return PeerProcessOutcome::Fatal;
      }
      vrr_response_.timings.push_back(*timing);
      continue;
    }
    if (type == GWIPC_MESSAGE_SESSION_STATE_CHANGE &&
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
    if (type == GWIPC_MESSAGE_BUFFER_RELEASE &&
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
      if (vrr_profile_ && !frame_acknowledged_) {
        error = "M14 compositor buffer release arrived before frame acknowledgement";
        return PeerProcessOutcome::Fatal;
      }
      releases_.push_back({release->buffer_id, release->reason});
      if (vrr_profile_ && frame_acknowledged_) {
        vrr_response_.released_buffer_ids.push_back(release->buffer_id);
        const auto* expectation = vrr_cache_.expectation();
        if (!expectation) {
          error = "M14 compositor buffer release has no response expectation";
          return PeerProcessOutcome::Fatal;
        }
        const auto expected = expectation->release_buffer_ids.size();
        if (vrr_response_.released_buffer_ids.size() == expected)
          return finish_vrr_response(error);
        if (vrr_response_.released_buffer_ids.size() > expected) {
          error = "M14 compositor returned too many buffer releases";
          return PeerProcessOutcome::Fatal;
        }
      }
      continue;
    }
    if (type != GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
      error = type == GWIPC_MESSAGE_BUFFER_RELEASE
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
    const auto expected_output = output_model_ ? UINT64_C(0) : UINT64_C(1);
    if (ack && ack->commit_id == expected_commit &&
        ack->output_id == expected_output &&
        ack->presented_generation == expected_generation &&
        ack->result >= GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA &&
        ack->result <= GWIPC_FRAME_DROPPED &&
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) != 0 &&
        gwipc_message_reply_to(message.get()) == commit_sequence_) {
      if (vrr_profile_) vrr_cache_.cancel_expectation();
      state_ = PeerBootstrapState::Synchronized;
      return PeerProcessOutcome::SemanticRejected;
    }
    if (!ack || ack->commit_id != expected_commit ||
        ack->output_id != expected_output ||
        ack->presented_generation != expected_generation ||
        ack->result != GWIPC_FRAME_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) != commit_sequence_) {
      error = "invalid compositor bootstrap acknowledgement";
      return PeerProcessOutcome::Fatal;
    }
    if (vrr_profile_) {
      vrr_response_.acknowledgement = *ack;
      frame_acknowledged_ = true;
      const auto* expectation = vrr_cache_.expectation();
      if (!expectation) {
        error = "M14 compositor acknowledgement has no response expectation";
        return PeerProcessOutcome::Fatal;
      }
      const auto expected_releases = expectation->release_buffer_ids.size();
      if (expected_releases == 0) return finish_vrr_response(error);
      continue;
    }
    if (!content_submission_)
      compositor_buffer_replay::promote(pending_, replay_input_);
    else
      compositor_buffer_replay::promote_content(pending_content_,
                                                replay_input_);
    content_submission_ = false;
    state_ = PeerBootstrapState::Synchronized;
  }
}

PeerProcessOutcome CompositorPeer::process(const short revents, std::string &error) {
  if (!transport_.process(revents, error)) {
    state_ = PeerBootstrapState::Failed;
    return PeerProcessOutcome::Disconnected;
  }
  if (state_ == PeerBootstrapState::Connecting && transport_.established()) {
    const bool started = output_model_ ? begin_output_inventory(error)
                                       : send_bootstrap(error);
    if (!started) {
      state_ = PeerBootstrapState::Failed;
      return PeerProcessOutcome::Fatal;
    }
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

bool CompositorPeer::prepare_reconnect_replay(std::string& error) {
  if (replay_input_.commit_id == 0) {
    error.clear();
    return true;
  }
  if (vrr_profile_) {
    if (!accepted_vrr_cache_ || reconnect_staged_vrr_cache_) {
      error = "accepted M14 replay checkpoint is unavailable";
      return false;
    }
    reconnect_staged_vrr_cache_ = vrr_cache_;
    vrr_cache_ = *accepted_vrr_cache_;
  }
  if (!compositor_buffer_replay::prepare(replay_input_, error)) {
    if (reconnect_staged_vrr_cache_) {
      vrr_cache_ = std::move(*reconnect_staged_vrr_cache_);
      reconnect_staged_vrr_cache_.reset();
    }
    return false;
  }
  return true;
}

void CompositorPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  commit_sequence_ = 0;
  content_submission_ = false;
  frame_acknowledged_ = false;
  vrr_response_ = {};
  vrr_cache_.cancel_expectation();
  releases_.clear();
  session_state_changes_.clear();
  pending_output_inventory_.reset();
  if (reconnect_staged_vrr_cache_) {
    vrr_cache_ = std::move(*reconnect_staged_vrr_cache_);
    reconnect_staged_vrr_cache_.reset();
  }
}
} // namespace glasswyrm::server
