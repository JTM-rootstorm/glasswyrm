#include "gwcomp/presentation_transaction.hpp"

#include "gwcomp/compositor.hpp"
#include "compositor/scene_vrr_validation.hpp"
#include "render/scene_renderer.hpp"

#include <algorithm>
#include <chrono>

namespace gw::compositor {
namespace {

std::optional<Rectangle> surface_bounds(const gwipc_surface_upsert& surface,
                                        const gwipc_output_upsert& output) {
  Rectangle local{0, 0, surface.logical_width, surface.logical_height};
  if (surface.clipping) {
    const auto clipped = intersection(
        local, Rectangle{surface.clip_x, surface.clip_y, surface.clip_width,
                         surface.clip_height});
    if (!clipped) return std::nullopt;
    local = *clipped;
  }
  const auto placed = translate(local, surface.logical_x, surface.logical_y);
  if (!placed) return std::nullopt;
  return intersection(
      *placed,
      Rectangle{0, 0, output.logical_width, output.logical_height});
}

} // namespace

PresentationTransaction::PresentationTransaction(
    SceneModel candidate, AttachmentMap attachments, ReleaseMap releases,
    glasswyrm::output::SoftwareFrame frame,
    std::optional<glasswyrm::output::SoftwareFrameSet> frame_set,
    gwipc_frame_commit commit,
    PresentedFrame presented, std::optional<PreparedSceneManifest> manifest,
    std::optional<PreparedVrrFrame> vrr,
    std::optional<VrrResponseBatch> vrr_response,
    const std::uint64_t token,
    const std::chrono::steady_clock::time_point deadline)
    : candidate_(std::move(candidate)), attachments_(std::move(attachments)),
      releases_(std::move(releases)), frame_(std::move(frame)),
      frame_set_(std::move(frame_set)), commit_(commit), presented_(presented),
      manifest_(std::move(manifest)), vrr_(std::move(vrr)),
      vrr_response_(std::move(vrr_response)), token_(token),
      deadline_(deadline) {}

PresentationTransaction::ReleaseMap
PresentationTransaction::calculate_retired_buffers(
    const Compositor& compositor, const Scene& staged) {
  ReleaseMap releases;
  for (const auto& [surface_id, old_buffer] :
       compositor.committed_attachments_) {
    const auto now = compositor.pending_attachments_.find(surface_id);
    if (now == compositor.pending_attachments_.end()) {
      releases[old_buffer] =
          staged.surfaces.contains(surface_id)
              ? GWIPC_BUFFER_RELEASE_CONSUMER_DONE
              : GWIPC_BUFFER_RELEASE_SURFACE_REMOVED;
    } else if (now->second != old_buffer) {
      releases[old_buffer] = GWIPC_BUFFER_RELEASE_REPLACED;
    }
  }
  for (const auto& [buffer_id, mapping] : compositor.mappings_) {
    (void)mapping;
    const bool remains_attached = std::any_of(
        compositor.pending_attachments_.begin(),
        compositor.pending_attachments_.end(),
        [buffer_id](const auto& item) { return item.second == buffer_id; });
    if (!remains_attached && !releases.contains(buffer_id))
      releases[buffer_id] = GWIPC_BUFFER_RELEASE_CONSUMER_DONE;
  }
  return releases;
}

void PresentationTransaction::release_retired_buffers(
    Compositor& compositor, const Scene& staged) {
  const auto releases = calculate_retired_buffers(compositor, staged);
  for (const auto& [buffer_id, reason] : releases)
    compositor.releases_.insert_or_assign(buffer_id, reason);
  for (const auto& [buffer_id, reason] : compositor.releases_) {
    (void)reason;
    compositor.mappings_.erase(buffer_id);
  }
}

PresentedFrame PresentationTransaction::promote(
    Compositor& compositor, const std::uint64_t visible_hash) {
  if (completed_vrr_) {
    std::string state_error;
    if (!compositor.committed_vrr_.promote(
            std::move(completed_vrr_->states),
            std::move(completed_vrr_->timings), commit_.commit_id,
            commit_.producer_generation, state_error) ||
        !vrr_response_ || !vrr_response_->ready()) {
      presented_.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      presented_.disposition = PresentedFrame::Disposition::Fatal;
      return presented_;
    }
    presented_.vrr_response = vrr_response_->messages();
  }
  for (const auto& [buffer_id, reason] : releases_)
    compositor.releases_.insert_or_assign(buffer_id, reason);
  for (const auto& [buffer_id, reason] : releases_) {
    (void)reason;
    compositor.mappings_.erase(buffer_id);
  }
  compositor.scene_ = std::move(candidate_);
  compositor.committed_attachments_ = std::move(attachments_);
  compositor.output_ = std::move(frame_);
  compositor.output_set_ = std::move(frame_set_);
  ++compositor.frame_ordinal_;
  compositor.last_generation_ = commit_.producer_generation;
  presented_.disposition = PresentedFrame::Disposition::Complete;
  presented_.ordinal = compositor.frame_ordinal_;
  presented_.hash = visible_hash;
  return presented_;
}

bool PresentationTransaction::finalize_vrr(
    const glasswyrm::output::VrrPresentationFeedbackMap& feedback,
    std::string& error) {
  if (!vrr_) {
    if (!feedback.empty()) {
      error = "presenter returned unrequested VRR feedback";
      return false;
    }
    return true;
  }
  auto completed = VrrRuntime::complete(*vrr_, feedback, commit_.commit_id,
                                        commit_.producer_generation, error);
  if (!completed || !vrr_response_ ||
      !vrr_response_->finalize(*completed, error))
    return false;
  completed_vrr_ = std::move(*completed);
  return true;
}

bool PresentationTransaction::publish_manifest(Compositor& compositor,
                                               std::string& error) {
  if (!manifest_) return true;
  if (!compositor.scene_manifest_ ||
      !compositor.scene_manifest_->publish(*manifest_, error)) {
    if (error.empty()) error = "scene manifest publisher is unavailable";
    presented_.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    presented_.disposition = PresentedFrame::Disposition::Fatal;
    return false;
  }
  return true;
}

std::optional<PresentationTransaction::ValidatedCommit>
PresentationTransaction::validate_scene(Compositor& compositor,
                                        const gwipc_frame_commit& value,
                                        PresentedFrame& presented,
                                        std::string& error) {
  if (compositor.snapshot_invalid_) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "complete snapshot contains duplicate surface records";
    return std::nullopt;
  }
  if (value.commit_id <= compositor.last_commit_id_ ||
      value.producer_generation < compositor.last_generation_) {
    error = "commit IDs must increase and generations must not decrease";
    return std::nullopt;
  }
  compositor.last_commit_id_ = value.commit_id;

  ValidatedCommit validated{compositor.scene_, {},
                            compositor.scene_.pending_damage_surface_ids(),
                            false, false};
  validated.result = validated.candidate.commit(value);
  presented.result = validated.result.result;
  presented.generation = validated.result.presented_generation;
  if (!validated.result.accepted())
    return std::nullopt;
  const auto vrr_validation = validate_scene_vrr(
      validated.candidate.committed(), compositor.vrr_contract_enabled_);
  if (!vrr_validation.accepted()) {
    presented.result = vrr_validation.result;
    error = vrr_validation.error;
    return std::nullopt;
  }
  validated.metadata_only_peer =
      compositor.profile_ == PeerProfile::M6MetadataProtocolServer;
  validated.protocol_server =
      compositor.profile_ != PeerProfile::M4TestProducer;
  return validated;
}

bool PresentationTransaction::validate_scene_surface(
    const Compositor& compositor, const Scene& staged,
    const std::uint64_t surface_id, const gwipc_surface_upsert& surface,
    const ValidatedCommit& validated, std::size_t& policy_surface_count,
    PresentedFrame& presented, std::string& error) {
  const bool metadata_only =
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  const bool cursor =
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  if (cursor && compositor.profile_ != PeerProfile::M7BufferedProtocolServer) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "cursor surfaces require a buffered ProtocolServer peer";
    return false;
  }
  if (validated.metadata_only_peer != metadata_only) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = validated.metadata_only_peer
                ? "ProtocolServer supplied a buffered surface"
                : "TestProducer supplied a metadata-only surface";
    return false;
  }
  const auto policy = staged.surface_policies.find(surface_id);
  if (cursor && policy != staged.surface_policies.end()) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "cursor surfaces cannot carry policy metadata";
    return false;
  }
  if (!cursor && validated.protocol_server &&
      (policy == staged.surface_policies.end() ||
       policy->second.x11_window_id != surface.x11_window_id)) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "ProtocolServer surface is missing matching policy metadata";
    return false;
  }
  if (!cursor)
    ++policy_surface_count;
  if (!validated.protocol_server && policy != staged.surface_policies.end()) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "TestProducer surfaces cannot carry policy metadata";
    return false;
  }
  return true;
}

bool PresentationTransaction::validate_surface_attachment(
    const Compositor& compositor, const std::uint64_t surface_id,
    const gwipc_surface_upsert& surface, PresentedFrame& presented,
    std::string& error) {
  const bool metadata_only =
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  const bool cursor =
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  const auto attachment = compositor.pending_attachments_.find(surface_id);
  const bool new_buffered_protocol_surface =
      compositor.profile_ == PeerProfile::M7BufferedProtocolServer &&
      !compositor.scene_.committed().surfaces.contains(surface_id);
  const bool requires_attachment =
      !metadata_only &&
      (surface.visible || (new_buffered_protocol_surface && !cursor));
  if (requires_attachment &&
      attachment == compositor.pending_attachments_.end()) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = new_buffered_protocol_surface
                ? "new buffered ProtocolServer surface has no attached buffer"
                : "visible surface has no attached buffer";
    return false;
  }
  if (metadata_only && attachment != compositor.pending_attachments_.end()) {
    presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
    error = "metadata-only surface has an attached buffer";
    return false;
  }
  if (attachment == compositor.pending_attachments_.end())
    return true;
  const auto mapping = compositor.mappings_.find(attachment->second);
  const auto client_scale =
      compositor.scene_.profile() == SceneProfile::OutputModel
          ? surface.scale_numerator
          : 1U;
  const auto expected_width =
      static_cast<std::uint64_t>(surface.logical_width) * client_scale;
  const auto expected_height =
      static_cast<std::uint64_t>(surface.logical_height) * client_scale;
  if (mapping == compositor.mappings_.end() ||
      mapping->second->width() != expected_width ||
      mapping->second->height() != expected_height) {
    presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
    error = "surface and buffer dimensions do not match";
    return false;
  }
  const auto required_format =
      cursor ? GWIPC_PIXEL_FORMAT_ARGB8888 : GWIPC_PIXEL_FORMAT_XRGB8888;
  if (compositor.profile_ == PeerProfile::M7BufferedProtocolServer &&
      mapping->second->pixel_format() != required_format) {
    presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
    error = cursor ? "cursor surfaces require premultiplied ARGB8888"
                   : "buffered ProtocolServer surfaces require XRGB8888";
    return false;
  }
  return true;
}

bool PresentationTransaction::validate_attachments(
    const Compositor& compositor, const Scene& staged,
    const ValidatedCommit& validated, PresentedFrame& presented,
    std::string& error) {
  std::size_t policy_surface_count = 0;
  for (const auto& [surface_id, surface] : staged.surfaces) {
    if (!validate_scene_surface(compositor, staged, surface_id, surface,
                                validated, policy_surface_count, presented,
                                error) ||
        !validate_surface_attachment(compositor, surface_id, surface, presented,
                                     error))
      return false;
  }
  if (validated.protocol_server &&
      staged.surface_policies.size() != policy_surface_count) {
    presented.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
    error = "surface policy references an unknown surface";
    return false;
  }
  return true;
}

PresentedFrame PresentationTransaction::promote_disabled_output(
    Compositor& compositor, ValidatedCommit&& validated,
    const gwipc_frame_commit& value, PresentedFrame presented) {
  const auto& staged = validated.candidate.committed();
  if (!validated.metadata_only_peer)
    release_retired_buffers(compositor, staged);
  compositor.scene_ = std::move(validated.candidate);
  compositor.committed_attachments_ = compositor.pending_attachments_;
  compositor.output_.disable();
  compositor.output_set_.reset();
  compositor.last_generation_ = value.producer_generation;
  presented.disposition = PresentedFrame::Disposition::Complete;
  return presented;
}

PresentedFrame PresentationTransaction::promote_metadata_only(
    Compositor& compositor, ValidatedCommit&& validated,
    const gwipc_frame_commit& value, PresentedFrame presented,
    std::string& error) {
  PreparedSceneManifest manifest;
  const auto prepared =
      validated.candidate.profile() == SceneProfile::OutputModel
          ? SceneManifest::prepare_output_model(
                value.commit_id, value.producer_generation,
                validated.candidate.committed(), manifest, error)
          : SceneManifest::prepare(value.commit_id, value.producer_generation,
                                   validated.candidate.committed(), manifest,
                                   error);
  if (!prepared) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    return presented;
  }
  if (compositor.scene_manifest_ &&
      !compositor.scene_manifest_->publish(manifest, error)) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    return presented;
  }
  compositor.scene_ = std::move(validated.candidate);
  compositor.committed_attachments_.clear();
  compositor.output_.disable();
  compositor.output_set_.reset();
  ++compositor.frame_ordinal_;
  compositor.last_generation_ = value.producer_generation;
  presented.ordinal = compositor.frame_ordinal_;
  presented.hash = manifest.result.hash;
  presented.disposition = PresentedFrame::Disposition::Complete;
  return presented;
}

std::optional<PresentationTransaction::PreparedOutputFrame>
PresentationTransaction::prepare_output_frame(Compositor& compositor,
                                              ValidatedCommit& validated,
                                              const gwipc_frame_commit& value,
                                              PresentedFrame& presented,
                                              std::string& error) {
  const auto& staged = validated.candidate.committed();
  for (const auto& [surface_id, buffer_id] : compositor.pending_attachments_) {
    (void)buffer_id;
    if (!staged.surfaces.contains(surface_id)) {
      presented.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
      error = "buffer references an unknown surface";
      return std::nullopt;
    }
  }

  if (validated.candidate.profile() == SceneProfile::OutputModel) {
    return prepare_output_frame_set(compositor, validated, value, presented,
                                    error);
  }

  DamageRegion attachment_damage(Rectangle{0, 0, staged.output->logical_width,
                                           staged.output->logical_height});
  for (const auto& rectangle : validated.result.damage)
    attachment_damage.add(rectangle);
  for (const auto& [surface_id, surface] : staged.surfaces) {
    if (!surface.visible)
      continue;
    const auto old = compositor.committed_attachments_.find(surface_id);
    const auto now = compositor.pending_attachments_.find(surface_id);
    const bool changed = old == compositor.committed_attachments_.end()
                             ? now != compositor.pending_attachments_.end()
                             : now == compositor.pending_attachments_.end() ||
                                   old->second != now->second;
    if (changed) {
      if (const auto bounds = surface_bounds(surface, *staged.output))
        attachment_damage.add(*bounds);
    }
  }
  validated.result.damage = attachment_damage.rectangles();

  const auto stacking_order = validated.candidate.stacking_order();
  const gw::render::RenderFrameRequest render_request{
      staged,
      stacking_order,
      compositor.mappings_,
      compositor.pending_attachments_,
      validated.result.damage,
      &compositor.output_,
      value.commit_id,
      value.producer_generation,
      compositor.frame_ordinal_ + 1U};
  auto rendered = compositor.renderer_->render(render_request);
  if (!rendered.complete()) {
    presented.result =
        rendered.disposition == gw::render::RenderDisposition::InvalidBuffer
            ? GWIPC_FRAME_REJECTED_INVALID_BUFFER
            : GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    if (rendered.disposition == gw::render::RenderDisposition::Fatal)
      presented.disposition = PresentedFrame::Disposition::Fatal;
    error = rendered.error.empty() ? "scene renderer rejected the frame"
                                   : std::move(rendered.error);
    return std::nullopt;
  }
  PreparedOutputFrame prepared;
  prepared.frame = std::move(rendered.frame);
  prepared.damage = validated.result.damage;
  prepared.canonical_hash = prepared.frame.visible_hash();
  prepared.releases = calculate_retired_buffers(compositor, staged);
  if (validated.protocol_server && compositor.scene_manifest_) {
    prepared.manifest.emplace();
    if (!SceneManifest::prepare(value.commit_id, value.producer_generation,
                                staged, *prepared.manifest, error)) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return std::nullopt;
    }
  }
  return prepared;
}

PresentedFrame PresentationTransaction::stage_presentation(
    Compositor& compositor, ValidatedCommit&& validated,
    PreparedOutputFrame&& prepared, const gwipc_frame_commit& value,
    PresentedFrame presented, std::string& error) {
  glasswyrm::output::PresentResult presentation;
  if (prepared.frame_set) {
    presentation = compositor.presenter_->present(*prepared.frame_set);
  } else {
    const auto& output = *validated.candidate.committed().output;
    const glasswyrm::output::SoftwareFrameView frame{
        prepared.frame.spec(output.refresh_millihertz),
        prepared.frame.pixels(), prepared.damage, value.commit_id,
        value.producer_generation, compositor.frame_ordinal_ + 1U};
    presentation = compositor.presenter_->present(frame);
  }
  if (presentation.disposition ==
      glasswyrm::output::PresentDisposition::Rejected) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = presentation.error;
    return presented;
  }
  if (presentation.disposition ==
      glasswyrm::output::PresentDisposition::Fatal) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    presented.disposition = PresentedFrame::Disposition::Fatal;
    error = presentation.error;
    return presented;
  }
  if (presentation.disposition ==
      glasswyrm::output::PresentDisposition::Complete) {
    if (presentation.visible_hash != prepared.canonical_hash) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      presented.disposition = PresentedFrame::Disposition::Fatal;
      error = "presentation hash differs from the canonical software frame";
      return presented;
    }
    PresentationTransaction transaction(
        std::move(validated.candidate), compositor.pending_attachments_,
        std::move(prepared.releases), std::move(prepared.frame),
        std::move(prepared.frame_set), value,
        presented, std::move(prepared.manifest), std::move(prepared.vrr),
        std::move(prepared.vrr_response), 0, compositor.timing_.now());
    if (!transaction.finalize_vrr(presentation.vrr_feedback, error)) {
      transaction.presented_.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      transaction.presented_.disposition = PresentedFrame::Disposition::Fatal;
      return transaction.presented_;
    }
    if (!transaction.publish_manifest(compositor, error))
      return transaction.presented_;
    return transaction.promote(compositor, presentation.visible_hash);
  }
  if (presentation.token == 0) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    presented.disposition = PresentedFrame::Disposition::Fatal;
    error = "pending presentation returned a zero completion token";
    return presented;
  }
  presented.disposition = PresentedFrame::Disposition::Pending;
  try {
    compositor.pending_presentation_ =
        std::unique_ptr<PresentationTransaction>(new PresentationTransaction(
            std::move(validated.candidate), compositor.pending_attachments_,
            std::move(prepared.releases), std::move(prepared.frame),
            std::move(prepared.frame_set), value,
            presented, std::move(prepared.manifest), std::move(prepared.vrr),
            std::move(prepared.vrr_response), presentation.token,
            compositor.timing_.now() + compositor.timing_.timeout));
  } catch (...) {
    compositor.presenter_->abort_pending(presentation.token);
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    presented.disposition = PresentedFrame::Disposition::Fatal;
    error = "could not retain pending presentation state";
  }
  return presented;
}

PresentedFrame PresentationTransaction::commit(Compositor& compositor,
                                               const gwipc_frame_commit& value,
                                               std::string& error) {
  PresentedFrame presented;
  auto validated = validate_scene(compositor, value, presented, error);
  if (!validated)
    return presented;
  const auto& staged = validated->candidate.committed();
  if (!validate_attachments(compositor, staged, *validated, presented, error))
    return presented;
  if (validated->candidate.profile() == SceneProfile::Historical &&
      (!staged.output || !staged.output->enabled))
    return promote_disabled_output(compositor, std::move(*validated), value,
                                   presented);
  if (validated->metadata_only_peer)
    return promote_metadata_only(compositor, std::move(*validated), value,
                                 presented, error);
  auto prepared =
      prepare_output_frame(compositor, *validated, value, presented, error);
  if (!prepared)
    return presented;
  return stage_presentation(compositor, std::move(*validated),
                            std::move(*prepared), value, presented, error);
}

} // namespace gw::compositor
