#include "gwcomp/compositor.hpp"

#include "gwcomp/presentation_transaction.hpp"

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
  if (role == GWIPC_ROLE_TEST_PRODUCER) {
    return (capabilities & (common | buffered)) == (common | buffered)
               ? std::optional{PeerProfile::M4TestProducer}
               : std::nullopt;
  }
  if (role != GWIPC_ROLE_PROTOCOL_SERVER ||
      (capabilities & (common | GWIPC_CAP_WINDOW_LIFECYCLE)) !=
          (common | GWIPC_CAP_WINDOW_LIFECYCLE))
    return std::nullopt;
  const auto negotiated_buffered = capabilities & buffered;
  if (negotiated_buffered == 0)
    return PeerProfile::M6MetadataProtocolServer;
  if (negotiated_buffered == buffered)
    return PeerProfile::M7BufferedProtocolServer;
  return std::nullopt;
}

Compositor::Compositor(
    std::filesystem::path dump_directory,
    std::optional<std::filesystem::path> scene_manifest)
    : presenter_(std::move(dump_directory)) {
  if (scene_manifest) scene_manifest_.emplace(std::move(*scene_manifest));
}

bool Compositor::begin_snapshot() {
  if (snapshot_active_ || !scene_.begin_complete_snapshot()) return false;
  pre_snapshot_attachments_ = pending_attachments_;
  if (profile_ == PeerProfile::M7BufferedProtocolServer)
    pending_attachments_ = committed_attachments_;
  else
    pending_attachments_.clear();
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_invalid_ = false;
  snapshot_active_ = true;
  return true;
}

bool Compositor::end_snapshot() {
  if (!snapshot_active_ || !scene_.end_complete_snapshot()) return false;
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
  return true;
}

void Compositor::abort_snapshot() {
  scene_.abort_complete_snapshot();
  pending_attachments_ = pre_snapshot_attachments_;
  pre_snapshot_attachments_.clear();
  snapshot_active_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_invalid_ = false;
}

bool Compositor::apply(const gwipc_output_upsert& value) { return scene_.apply(value); }
bool Compositor::apply(const gwipc_output_remove& value) { return scene_.apply(value); }
bool Compositor::apply(const gwipc_surface_upsert& value) {
  if (snapshot_active_ && !snapshot_surface_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_policy_upsert& value) {
  if (snapshot_active_ && !snapshot_policy_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_remove& value) {
  if (!scene_.apply(value)) return false;
  pending_attachments_.erase(value.surface_id);
  return true;
}

bool Compositor::apply(const gwipc_surface_damage& value) {
  if (profile_ == PeerProfile::M6MetadataProtocolServer) return false;
  return scene_.apply(value);
}

bool Compositor::attach(const gwipc_buffer_attach& value, int fd, std::string& error) {
  if (profile_ == PeerProfile::M6MetadataProtocolServer) {
    if (fd >= 0) (void)::close(fd);
    error = "metadata-only ProtocolServer peers cannot attach buffers";
    return false;
  }
  if (!scene_.snapshot_active() && !scene_.initial_snapshot_received()) {
    if (fd >= 0) (void)::close(fd);
    error = "buffer mutation is gated until a complete snapshot begins";
    return false;
  }
  const auto surface = scene_.pending().surfaces.find(value.surface_id);
  if (surface != scene_.pending().surfaces.end() &&
      surface->second.presentation_flags ==
          GWIPC_SURFACE_PRESENTATION_METADATA_ONLY) {
    if (fd >= 0) (void)::close(fd);
    error = "metadata-only surfaces cannot have buffers";
    return false;
  }
  if (mappings_.contains(value.buffer_id)) {
    if (fd >= 0) (void)::close(fd);
    error = "buffer ID is already active";
    return false;
  }
  auto unique = BufferMapping::import(value, fd, error);
  if (!unique) {
    if (value.buffer_id != 0) releases_[value.buffer_id] = GWIPC_BUFFER_RELEASE_INVALID;
    return false;
  }
  mappings_.emplace(value.buffer_id, Mapping(std::move(unique)));
  pending_attachments_[value.surface_id] = value.buffer_id;
  return true;
}

bool Compositor::detach(const gwipc_buffer_detach& value) {
  if (!scene_.snapshot_active() && !scene_.initial_snapshot_received()) return false;
  const auto found = pending_attachments_.find(value.surface_id);
  if (found == pending_attachments_.end() || found->second != value.buffer_id) return false;
  pending_attachments_.erase(found);
  return true;
}

PresentedFrame Compositor::commit(const gwipc_frame_commit& value,
                                  std::string& error) {
  return PresentationTransaction::commit(*this, value, error);
}

void Compositor::disconnect() {
  scene_.disconnect();
  mappings_.clear();
  pending_attachments_.clear();
  committed_attachments_.clear();
  pre_snapshot_attachments_.clear();
  releases_.clear();
  output_.disable();
  last_commit_id_ = 0;
  last_generation_ = 0;
  snapshot_active_ = false;
  snapshot_invalid_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
}

} // namespace gw::compositor
