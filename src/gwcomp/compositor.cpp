#include "gwcomp/compositor.hpp"

#include "gwcomp/presentation_transaction.hpp"
#include "compositor/scene_vrr_validation.hpp"
#include "render/software/scene_renderer.hpp"

#if GW_HAS_HEADLESS_BACKEND
#include "backends/headless/presenter.hpp"
#endif

#include <unistd.h>
#include <utility>

namespace gw::compositor {
std::optional<PeerProfile>
select_peer_profile(const gwipc_role role,
                    const std::uint64_t capabilities) noexcept {
  constexpr std::uint64_t common =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
      GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
  constexpr std::uint64_t buffered =
      GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
      GWIPC_CAP_DAMAGE_REGIONS;
  constexpr std::uint64_t output_model =
      GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
      GWIPC_CAP_SCALE_METADATA;
  constexpr std::uint64_t vrr = GWIPC_CAP_VRR_METADATA |
                                GWIPC_CAP_VRR_POLICY |
                                GWIPC_CAP_PRESENTATION_TIMING;
  if (role == GWIPC_ROLE_TEST_PRODUCER) {
    if ((capabilities & (GWIPC_CAP_CURSOR_SURFACE | output_model | vrr)) != 0)
      return std::nullopt;
    return (capabilities & (common | buffered)) == (common | buffered)
               ? std::optional{PeerProfile::M4TestProducer}
               : std::nullopt;
  }
  if (role != GWIPC_ROLE_PROTOCOL_SERVER ||
      (capabilities & (common | GWIPC_CAP_WINDOW_LIFECYCLE)) !=
          (common | GWIPC_CAP_WINDOW_LIFECYCLE))
    return std::nullopt;
  const auto negotiated_output_model = capabilities & output_model;
  if (negotiated_output_model != 0 &&
      negotiated_output_model != output_model)
    return std::nullopt;
  const auto negotiated_vrr = capabilities & vrr;
  if (negotiated_vrr != 0 &&
      (negotiated_vrr != vrr || negotiated_output_model != output_model))
    return std::nullopt;
  const auto negotiated_buffered = capabilities & buffered;
  if ((capabilities & GWIPC_CAP_CURSOR_SURFACE) != 0 &&
      negotiated_buffered != buffered)
    return std::nullopt;
  if (negotiated_buffered == 0)
    return PeerProfile::M6MetadataProtocolServer;
  if (negotiated_buffered == buffered)
    return PeerProfile::M7BufferedProtocolServer;
  return std::nullopt;
}

#if GW_HAS_HEADLESS_BACKEND
Compositor::Compositor(
    std::filesystem::path dump_directory,
    std::optional<std::filesystem::path> scene_manifest)
    : Compositor(std::make_unique<glasswyrm::headless::Presenter>(
                     std::move(dump_directory)),
                 std::move(scene_manifest)) {}
#endif

Compositor::Compositor(
    std::unique_ptr<glasswyrm::output::PresentationBackend> presenter,
    std::optional<std::filesystem::path> scene_manifest,
    PresentationTiming timing,
    std::unique_ptr<gw::render::SceneRenderer> renderer,
    std::unique_ptr<gw::render::OutputSceneRenderer> output_renderer)
    : renderer_(renderer ? std::move(renderer)
                         : std::make_unique<
                               gw::render::software::SoftwareSceneRenderer>()),
      output_renderer_(std::move(output_renderer)),
      presenter_(std::move(presenter)), timing_(std::move(timing)) {
  if (scene_manifest) scene_manifest_.emplace(std::move(*scene_manifest));
}

Compositor::~Compositor() {
  PresentationTransaction::abort(*this);
  std::string ignored;
  (void)shutdown_presentation(ignored);
}

bool Compositor::configure_vrr_contract(const bool enabled,
                                        std::string& error) {
  if (presentation_pending() || presentation_suspended_ ||
      !presenter_->configure_vrr_contract(enabled, error))
    return false;
  vrr_contract_enabled_ = enabled;
  error.clear();
  return true;
}

std::optional<VrrInventorySnapshot> Compositor::vrr_inventory(
    const glasswyrm::output::OutputLayout& layout,
    std::string& error) const {
  if (!vrr_contract_enabled_ || !presenter_) {
    error = "VRR inventory was requested without a negotiated contract";
    return std::nullopt;
  }
  VrrInventorySnapshot result;
  result.capabilities.reserve(layout.output_order.size());
  result.policies.reserve(layout.output_order.size());
  result.states.reserve(layout.output_order.size());
  result.timings.reserve(layout.output_order.size());
  const auto& scene = scene_.committed();
  for (const auto output_id : layout.output_order) {
    const auto capability = presenter_->vrr_capability(output_id.value);
    if (!capability) {
      error = "presentation backend omitted an output VRR capability";
      return std::nullopt;
    }
    gwipc_output_vrr_capability_upsert capability_record{};
    capability_record.struct_size = sizeof(capability_record);
    capability_record.output_id = output_id.value;
    capability_record.connector_property_present =
        capability->connector_property_present;
    capability_record.hardware_capable = capability->hardware_capable;
    capability_record.kms_controllable = capability->kms_controllable;
    capability_record.simulated = capability->simulated;
    capability_record.range_available = capability->range_available;
    capability_record.atomic_required = capability->atomic_required;
    capability_record.minimum_refresh_millihertz =
        capability->minimum_refresh_millihertz;
    capability_record.maximum_refresh_millihertz =
        capability->maximum_refresh_millihertz;
    capability_record.reason_flags = capability->reason_flags;
    result.capabilities.push_back(capability_record);

    gwipc_output_vrr_policy_upsert policy{};
    policy.struct_size = sizeof(policy);
    policy.output_id = output_id.value;
    policy.mode = GWIPC_VRR_POLICY_OFF;
    if (const auto current =
            scene.vrr.output_policies.find(output_id.value);
        current != scene.vrr.output_policies.end())
      policy = current->second;
    result.policies.push_back(policy);

    const auto committed = committed_vrr_.outputs().find(output_id.value);
    if (committed != committed_vrr_.outputs().end()) {
      if (committed->second.requested_mode != policy.mode) {
        error = "committed VRR state does not match its output policy";
        return std::nullopt;
      }
      result.states.push_back(committed->second);
      if (const auto timing =
              committed_vrr_.timings().find(output_id.value);
          timing != committed_vrr_.timings().end())
        result.timings.push_back(timing->second);
      continue;
    }

    gwipc_output_vrr_state_upsert state{};
    state.struct_size = sizeof(state);
    state.output_id = output_id.value;
    state.requested_mode = policy.mode;
    state.decision = GWIPC_VRR_DECISION_DISABLED;
    state.session_active = capability->session_active;
    state.reason_flags = capability->reason_flags;
    if (!layout.states.at(output_id).enabled)
      state.reason_flags |= GWIPC_VRR_REASON_OUTPUT_DISABLED;
    if (policy.mode == GWIPC_VRR_POLICY_OFF)
      state.reason_flags |= GWIPC_VRR_REASON_POLICY_OFF;
    state.state_generation = layout.generation;
    result.states.push_back(state);
  }
  error.clear();
  return result;
}

bool Compositor::configure_scene_profile(
    const SceneProfile profile, const std::uint64_t primary_output_id,
    const std::uint64_t output_layout_generation) noexcept {
  if (presentation_pending() || snapshot_active_ ||
      scene_.initial_snapshot_received() ||
      (profile == SceneProfile::OutputModel &&
       (primary_output_id == 0 || output_layout_generation == 0)) ||
      (profile == SceneProfile::Historical &&
       (primary_output_id != 0 || output_layout_generation != 0)))
    return false;
  scene_ = SceneModel(profile);
  primary_output_id_ = primary_output_id;
  output_layout_generation_ = output_layout_generation;
  return true;
}

bool Compositor::begin_snapshot(const std::uint64_t generation) {
  if (presentation_pending() || snapshot_active_ ||
      !(scene_.profile() == SceneProfile::OutputModel
            ? scene_.begin_complete_snapshot(primary_output_id_,
                                             generation)
            : scene_.begin_complete_snapshot()))
    return false;
  pre_snapshot_attachments_ = pending_attachments_;
  if (profile_ == PeerProfile::M7BufferedProtocolServer)
    pending_attachments_ = committed_attachments_;
  else
    pending_attachments_.clear();
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_output_ids_.clear();
  snapshot_surface_output_ids_.clear();
  snapshot_invalid_ = false;
  snapshot_active_ = true;
  return true;
}

bool Compositor::end_snapshot() {
  if (presentation_pending() || !snapshot_active_ ||
      !validate_scene_vrr(scene_.pending(), vrr_contract_enabled_).accepted() ||
      !scene_.end_complete_snapshot())
    return false;
  if (profile_ == PeerProfile::M7BufferedProtocolServer) {
    for (auto attachment = pending_attachments_.begin();
         attachment != pending_attachments_.end();) {
      if (!scene_.pending().surfaces.contains(attachment->first))
        attachment = pending_attachments_.erase(attachment);
      else
        ++attachment;
    }
  }
  pre_snapshot_attachments_.clear();
  snapshot_active_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_output_ids_.clear();
  snapshot_surface_output_ids_.clear();
  return true;
}

void Compositor::abort_snapshot() {
  if (presentation_pending()) return;
  scene_.abort_complete_snapshot();
  pending_attachments_ = pre_snapshot_attachments_;
  pre_snapshot_attachments_.clear();
  snapshot_active_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_output_ids_.clear();
  snapshot_surface_output_ids_.clear();
  snapshot_invalid_ = false;
}

bool Compositor::apply(const gwipc_output_upsert& value) {
  if (presentation_pending()) return false;
  if (scene_.profile() == SceneProfile::OutputModel && snapshot_active_) {
    const bool first = snapshot_output_ids_.empty();
    if (!snapshot_output_ids_.insert(value.output_id).second ||
        (first && !scene_.set_snapshot_output_configuration(
                      value.output_id, scene_.pending().configuration_generation))) {
      snapshot_invalid_ = true;
      return false;
    }
    if (first) {
      primary_output_id_ = value.output_id;
      output_layout_generation_ = scene_.pending().configuration_generation;
    }
  }
  return scene_.apply(value);
}
bool Compositor::apply(const gwipc_output_remove& value) {
  return !presentation_pending() && scene_.apply(value);
}
bool Compositor::apply(const gwipc_surface_upsert& value) {
  if (presentation_pending()) return false;
  if (snapshot_active_ && !snapshot_surface_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_output_state& value) {
  if (presentation_pending()) return false;
  if (snapshot_active_ &&
      !snapshot_surface_output_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_policy_upsert& value) {
  if (presentation_pending()) return false;
  if (snapshot_active_ && !snapshot_policy_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_output_vrr_policy_upsert& value) {
  if (presentation_pending() || !snapshot_active_) return false;
  const bool applied = scene_.apply(value);
  if (!applied) snapshot_invalid_ = true;
  return applied;
}

bool Compositor::apply(const gwipc_surface_vrr_state& value) {
  if (presentation_pending() || !snapshot_active_) return false;
  const bool applied = scene_.apply(value);
  if (!applied) snapshot_invalid_ = true;
  return applied;
}

bool Compositor::apply(const gwipc_surface_remove& value) {
  if (presentation_pending() || !scene_.apply(value)) return false;
  pending_attachments_.erase(value.surface_id);
  return true;
}

bool Compositor::apply(const gwipc_surface_damage& value) {
  if (presentation_pending() ||
      profile_ == PeerProfile::M6MetadataProtocolServer)
    return false;
  return scene_.apply(value);
}

bool Compositor::attach(const gwipc_buffer_attach& value, int fd,
                        int synchronization_fd, std::string& error) {
  const auto close_descriptors = [&] {
    if (fd >= 0) (void)::close(fd);
    if (synchronization_fd >= 0) (void)::close(synchronization_fd);
  };
  if (presentation_pending()) {
    close_descriptors();
    error = "buffer mutation cannot overtake a pending presentation";
    return false;
  }
  if (profile_ == PeerProfile::M6MetadataProtocolServer) {
    close_descriptors();
    error = "metadata-only ProtocolServer peers cannot attach buffers";
    return false;
  }
  if (!scene_.snapshot_active() && !scene_.initial_snapshot_received()) {
    close_descriptors();
    error = "buffer mutation is gated until a complete snapshot begins";
    return false;
  }
  const auto surface = scene_.pending().surfaces.find(value.surface_id);
  if (surface != scene_.pending().surfaces.end() &&
      surface->second.presentation_flags ==
          GWIPC_SURFACE_PRESENTATION_METADATA_ONLY) {
    close_descriptors();
    error = "metadata-only surfaces cannot have buffers";
    return false;
  }
  if (mappings_.contains(value.buffer_id)) {
    close_descriptors();
    error = "buffer ID is already active";
    return false;
  }
  if (value.synchronization == GWIPC_SYNCHRONIZATION_EVENTFD &&
      !cpu_buffer_synchronization_) {
    close_descriptors();
    error = "eventfd synchronization was not negotiated";
    return false;
  }
  auto unique = BufferMapping::import(value, fd, synchronization_fd, error);
  if (!unique) {
    if (value.buffer_id != 0) releases_[value.buffer_id] = GWIPC_BUFFER_RELEASE_INVALID;
    return false;
  }
  mappings_.emplace(value.buffer_id, Mapping(std::move(unique)));
  pending_attachments_[value.surface_id] = value.buffer_id;
  return true;
}

bool Compositor::detach(const gwipc_buffer_detach& value) {
  if (presentation_pending() ||
      (!scene_.snapshot_active() && !scene_.initial_snapshot_received()))
    return false;
  const auto found = pending_attachments_.find(value.surface_id);
  if (found == pending_attachments_.end() || found->second != value.buffer_id) return false;
  pending_attachments_.erase(found);
  return true;
}

bool Compositor::suspend_presentation(std::string& error) {
  if (presentation_pending()) {
    error = "cannot suspend while a presentation is pending";
    return false;
  }
  if (presentation_suspended_) {
    error = "presentation backend is already suspended";
    return false;
  }
  if (presenter_->suspend(error) !=
      glasswyrm::output::BackendStateResult::Complete)
    return false;
  presentation_suspended_ = true;
  error.clear();
  return true;
}

bool Compositor::resume_presentation(std::string& error) {
  if (!presentation_suspended_) {
    error = "presentation backend is not suspended";
    return false;
  }
  const glasswyrm::output::SoftwareFrameView committed{
      output_.spec(), output_.pixels(), {}, 0, last_generation_,
      frame_ordinal_};
  const auto resumed = output_set_ ? presenter_->resume(output_set_->view())
                                   : presenter_->resume(committed);
  const auto expected_hash = output_set_ ? output_set_->aggregate_hash()
                                         : output_.visible_hash();
  if (resumed.disposition !=
          glasswyrm::output::PresentDisposition::Complete ||
      resumed.visible_hash != expected_hash) {
    error = resumed.error.empty()
                ? "presentation backend did not restore the committed frame"
                : resumed.error;
    return false;
  }
  presentation_suspended_ = false;
  error.clear();
  return true;
}

bool Compositor::activate_presentation_session(std::string& error) {
  if (presentation_suspended_) {
    error = "presentation backend is still suspended";
    return false;
  }
  return presenter_->activate_session(error);
}

bool Compositor::shutdown_presentation(std::string& error) noexcept {
  if (presentation_shutdown_) {
    error.clear();
    return true;
  }
  presentation_shutdown_ = true;
  if (!presenter_) {
    error.clear();
    return true;
  }
  return presenter_->shutdown(error) ==
         glasswyrm::output::BackendStateResult::Complete;
}

void Compositor::disconnect() {
  PresentationTransaction::abort(*this);
  pending_buffer_readiness_.reset();
  renderer_->disconnect();
  if (output_renderer_)
    output_renderer_->disconnect();
  scene_.disconnect();
  mappings_.clear();
  pending_attachments_.clear();
  committed_attachments_.clear();
  pre_snapshot_attachments_.clear();
  releases_.clear();
  committed_vrr_.clear();
  output_.disable();
  output_set_.reset();
  last_commit_id_ = 0;
  last_generation_ = 0;
  snapshot_active_ = false;
  snapshot_invalid_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_output_ids_.clear();
  snapshot_surface_output_ids_.clear();
  vrr_contract_enabled_ = false;
}

} // namespace gw::compositor
