#include "gwcomp/compositor.hpp"

#include "gwcomp/presentation_transaction.hpp"

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
    PresentationTiming timing)
    : presenter_(std::move(presenter)), timing_(std::move(timing)) {
  if (scene_manifest) scene_manifest_.emplace(std::move(*scene_manifest));
}

Compositor::~Compositor() {
  PresentationTransaction::abort(*this);
  if (presenter_) presenter_->shutdown();
}

bool Compositor::begin_snapshot() {
  if (pending_presentation_ || snapshot_active_ ||
      !scene_.begin_complete_snapshot())
    return false;
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
  if (pending_presentation_ || !snapshot_active_ ||
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
  return true;
}

void Compositor::abort_snapshot() {
  if (pending_presentation_) return;
  scene_.abort_complete_snapshot();
  pending_attachments_ = pre_snapshot_attachments_;
  pre_snapshot_attachments_.clear();
  snapshot_active_ = false;
  snapshot_surface_ids_.clear();
  snapshot_policy_ids_.clear();
  snapshot_invalid_ = false;
}

bool Compositor::apply(const gwipc_output_upsert& value) {
  return !pending_presentation_ && scene_.apply(value);
}
bool Compositor::apply(const gwipc_output_remove& value) {
  return !pending_presentation_ && scene_.apply(value);
}
bool Compositor::apply(const gwipc_surface_upsert& value) {
  if (pending_presentation_) return false;
  if (snapshot_active_ && !snapshot_surface_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_policy_upsert& value) {
  if (pending_presentation_) return false;
  if (snapshot_active_ && !snapshot_policy_ids_.insert(value.surface_id).second) {
    snapshot_invalid_ = true;
    return false;
  }
  return scene_.apply(value);
}

bool Compositor::apply(const gwipc_surface_remove& value) {
  if (pending_presentation_ || !scene_.apply(value)) return false;
  pending_attachments_.erase(value.surface_id);
  return true;
}

bool Compositor::apply(const gwipc_surface_damage& value) {
  if (pending_presentation_ ||
      profile_ == PeerProfile::M6MetadataProtocolServer)
    return false;
  return scene_.apply(value);
}

bool Compositor::attach(const gwipc_buffer_attach& value, int fd, std::string& error) {
  if (pending_presentation_) {
    if (fd >= 0) (void)::close(fd);
    error = "buffer mutation cannot overtake a pending presentation";
    return false;
  }
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
  if (pending_presentation_ ||
      (!scene_.snapshot_active() && !scene_.initial_snapshot_received()))
    return false;
  const auto found = pending_attachments_.find(value.surface_id);
  if (found == pending_attachments_.end() || found->second != value.buffer_id) return false;
  pending_attachments_.erase(found);
  return true;
}

PresentedFrame Compositor::commit(const gwipc_frame_commit& value,
                                  std::string& error) {
  if (pending_presentation_) {
    error = "frame commit cannot overtake a pending presentation";
    return {};
  }
  return PresentationTransaction::commit(*this, value, error);
}

bool Compositor::presentation_pending() const noexcept {
  return pending_presentation_ != nullptr;
}

int Compositor::presentation_poll_fd() const noexcept {
  return presenter_->poll_fd();
}

short Compositor::presentation_poll_events() const noexcept {
  return presenter_->poll_events();
}

int Compositor::presentation_timeout_ms() const {
  return PresentationTransaction::timeout_ms(*this);
}

PresentationCompletion Compositor::service_presentation(
    const short revents, std::string& error) {
  return PresentationTransaction::service(*this, revents, error);
}

bool Compositor::suspend_presentation(std::string& error) {
  if (pending_presentation_) {
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
  const auto resumed = presenter_->resume(committed);
  if (resumed.disposition !=
          glasswyrm::output::PresentDisposition::Complete ||
      resumed.visible_hash != output_.visible_hash()) {
    error = resumed.error.empty()
                ? "presentation backend did not restore the committed frame"
                : resumed.error;
    return false;
  }
  presentation_suspended_ = false;
  error.clear();
  return true;
}

void Compositor::disconnect() {
  PresentationTransaction::abort(*this);
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
