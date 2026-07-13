#include "gwcomp/presentation_transaction.hpp"

#include "gwcomp/compositor.hpp"
#include "render/software/renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace gw::compositor {
namespace {

using gw::render::software::FramebufferView;
using gw::render::software::ImageView;
using gw::render::software::PixelFormat;
using gw::render::software::RenderResult;

std::span<std::byte> pixel_bytes(std::span<std::uint32_t> pixels) {
  return {reinterpret_cast<std::byte*>(pixels.data()), pixels.size_bytes()};
}

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

void PresentationTransaction::release_retired_buffers(
    Compositor& compositor, const Scene& staged) {
  for (const auto& [surface_id, old_buffer] :
       compositor.committed_attachments_) {
    const auto now = compositor.pending_attachments_.find(surface_id);
    if (now == compositor.pending_attachments_.end()) {
      compositor.releases_[old_buffer] =
          staged.surfaces.contains(surface_id)
              ? GWIPC_BUFFER_RELEASE_CONSUMER_DONE
              : GWIPC_BUFFER_RELEASE_SURFACE_REMOVED;
    } else if (now->second != old_buffer) {
      compositor.releases_[old_buffer] = GWIPC_BUFFER_RELEASE_REPLACED;
    }
  }
  for (const auto& [buffer_id, mapping] : compositor.mappings_) {
    (void)mapping;
    const bool remains_attached = std::any_of(
        compositor.pending_attachments_.begin(),
        compositor.pending_attachments_.end(),
        [buffer_id](const auto& item) { return item.second == buffer_id; });
    if (!remains_attached && !compositor.releases_.contains(buffer_id))
      compositor.releases_[buffer_id] = GWIPC_BUFFER_RELEASE_CONSUMER_DONE;
  }
  for (const auto& [buffer_id, reason] : compositor.releases_) {
    (void)reason;
    compositor.mappings_.erase(buffer_id);
  }
}

PresentedFrame PresentationTransaction::commit(
    Compositor& compositor, const gwipc_frame_commit& value,
    std::string& error) {
  auto& scene_ = compositor.scene_;
  auto& mappings_ = compositor.mappings_;
  auto& pending_attachments_ = compositor.pending_attachments_;
  auto& committed_attachments_ = compositor.committed_attachments_;
  auto& output_ = compositor.output_;
  auto& presenter_ = compositor.presenter_;
  auto& scene_manifest_ = compositor.scene_manifest_;
  auto& frame_ordinal_ = compositor.frame_ordinal_;
  auto& last_commit_id_ = compositor.last_commit_id_;
  auto& last_generation_ = compositor.last_generation_;
  const auto snapshot_invalid_ = compositor.snapshot_invalid_;
  const auto profile_ = compositor.profile_;

  PresentedFrame presented;
  if (snapshot_invalid_) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "complete snapshot contains duplicate surface records";
    return presented;
  }
  if (value.commit_id <= last_commit_id_ ||
      value.producer_generation < last_generation_) {
    error = "commit IDs must increase and generations must not decrease";
    return presented;
  }
  last_commit_id_ = value.commit_id;

  SceneModel candidate = scene_;
  auto result = candidate.commit(value);
  presented.result = result.result;
  presented.generation = result.presented_generation;
  if (!result.accepted()) return presented;

  const auto& staged = candidate.committed();
  const bool metadata_only_peer =
      profile_ == PeerProfile::M6MetadataProtocolServer;
  const bool protocol_server = profile_ != PeerProfile::M4TestProducer;
  for (const auto& [surface_id, surface] : staged.surfaces) {
    const bool metadata_only =
        surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    if (metadata_only_peer != metadata_only) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      error = metadata_only_peer
                  ? "ProtocolServer supplied a buffered surface"
                  : "TestProducer supplied a metadata-only surface";
      return presented;
    }
    const auto policy = staged.surface_policies.find(surface_id);
    if (protocol_server &&
        (policy == staged.surface_policies.end() ||
         policy->second.x11_window_id != surface.x11_window_id)) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      error = "ProtocolServer surface is missing matching policy metadata";
      return presented;
    }
    if (!protocol_server && policy != staged.surface_policies.end()) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      error = "TestProducer surfaces cannot carry policy metadata";
      return presented;
    }
    const auto attachment = pending_attachments_.find(surface_id);
    const bool new_buffered_protocol_surface =
        profile_ == PeerProfile::M7BufferedProtocolServer &&
        !scene_.committed().surfaces.contains(surface_id);
    if (!metadata_only && (surface.visible || new_buffered_protocol_surface) &&
        attachment == pending_attachments_.end()) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      error = new_buffered_protocol_surface
                  ? "new buffered ProtocolServer surface has no attached buffer"
                  : "visible surface has no attached buffer";
      return presented;
    }
    if (metadata_only && attachment != pending_attachments_.end()) {
      presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
      error = "metadata-only surface has an attached buffer";
      return presented;
    }
    if (attachment == pending_attachments_.end()) continue;
    const auto mapping = mappings_.find(attachment->second);
    if (mapping == mappings_.end() ||
        mapping->second->width() != surface.logical_width ||
        mapping->second->height() != surface.logical_height) {
      presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
      error = "surface and buffer dimensions do not match";
      return presented;
    }
    if (profile_ == PeerProfile::M7BufferedProtocolServer &&
        mapping->second->pixel_format() != GWIPC_PIXEL_FORMAT_XRGB8888) {
      presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
      error = "buffered ProtocolServer surfaces require XRGB8888";
      return presented;
    }
  }
  if (protocol_server &&
      staged.surface_policies.size() != staged.surfaces.size()) {
    presented.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
    error = "surface policy references an unknown surface";
    return presented;
  }

  if (!staged.output || !staged.output->enabled) {
    if (!metadata_only_peer) release_retired_buffers(compositor, staged);
    scene_ = std::move(candidate);
    committed_attachments_ = pending_attachments_;
    output_.disable();
    last_generation_ = value.producer_generation;
    return presented;
  }

  if (metadata_only_peer) {
    SceneManifestResult manifest_result;
    if (scene_manifest_) {
      if (!scene_manifest_->append(value.commit_id, value.producer_generation,
                                   staged, manifest_result, error)) {
        presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
        return presented;
      }
    } else {
      std::string ignored_json;
      if (!SceneManifest::describe(value.commit_id, value.producer_generation,
                                   staged, manifest_result, ignored_json,
                                   error)) {
        presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
        return presented;
      }
    }
    scene_ = std::move(candidate);
    committed_attachments_.clear();
    output_.disable();
    ++frame_ordinal_;
    last_generation_ = value.producer_generation;
    presented.ordinal = frame_ordinal_;
    presented.hash = manifest_result.hash;
    return presented;
  }
  for (const auto& [surface_id, buffer_id] : pending_attachments_) {
    (void)buffer_id;
    if (!staged.surfaces.contains(surface_id)) {
      presented.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
      error = "buffer references an unknown surface";
      return presented;
    }
  }

  DamageRegion attachment_damage(Rectangle{0, 0, staged.output->logical_width,
                                           staged.output->logical_height});
  for (const auto& rectangle : result.damage) attachment_damage.add(rectangle);
  for (const auto& [surface_id, surface] : staged.surfaces) {
    if (!surface.visible) continue;
    const auto old = committed_attachments_.find(surface_id);
    const auto now = pending_attachments_.find(surface_id);
    const bool changed = old == committed_attachments_.end()
                             ? now != pending_attachments_.end()
                             : now == pending_attachments_.end() ||
                                   old->second != now->second;
    if (changed) {
      if (const auto bounds = surface_bounds(surface, *staged.output))
        attachment_damage.add(*bounds);
    }
  }
  result.damage = attachment_damage.rectangles();

  glasswyrm::output::SoftwareFrame scratch;
  if (!scratch.configure(staged.output->output_id,
                         staged.output->logical_width,
                         staged.output->logical_height, error)) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    return presented;
  }
  if (output_.enabled() && output_.id() == scratch.id() &&
      output_.width() == scratch.width() &&
      output_.height() == scratch.height()) {
    std::copy(output_.pixels().begin(), output_.pixels().end(),
              scratch.pixels().begin());
  }
  FramebufferView framebuffer{pixel_bytes(scratch.pixels()), scratch.width(),
                              scratch.height(), scratch.width() * 4U};
  for (const auto& rectangle : result.damage) {
    if (gw::render::software::clear(framebuffer, rectangle) !=
        RenderResult::Success) {
      presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
      error = "framebuffer damage is invalid";
      return presented;
    }
    for (const auto surface_id : candidate.stacking_order()) {
      const auto& surface = staged.surfaces.at(surface_id);
      if (!surface.visible || surface.opacity == 0 ||
          surface.presentation_flags ==
              GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
        continue;
      auto bounds = surface_bounds(surface, *staged.output);
      if (!bounds || !intersection(*bounds, rectangle)) continue;
      const auto mapping = mappings_.at(pending_attachments_.at(surface_id));
      Rectangle local{0, 0, surface.logical_width, surface.logical_height};
      if (surface.clipping) {
        local = {surface.clip_x, surface.clip_y, surface.clip_width,
                 surface.clip_height};
      }
      const ImageView image{
          mapping->bytes(), mapping->width(), mapping->height(),
          mapping->stride(),
          mapping->pixel_format() == GWIPC_PIXEL_FORMAT_XRGB8888
              ? PixelFormat::Xrgb8888
              : PixelFormat::Argb8888Premultiplied};
      const auto render = gw::render::software::composite(
          framebuffer, image, local, surface.logical_x + local.x,
          surface.logical_y + local.y, surface.opacity);
      if (render != RenderResult::Success) {
        presented.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
        error = render == RenderResult::InvalidPremultipliedPixel
                    ? "ARGB buffer contains a non-premultiplied pixel"
                    : "buffer view is invalid";
        return presented;
      }
    }
  }

  const glasswyrm::output::SoftwareFrameView frame{
      scratch.spec(staged.output->refresh_millihertz),
      scratch.pixels(),
      result.damage,
      value.commit_id,
      value.producer_generation,
      frame_ordinal_ + 1U};
  const auto presentation = presenter_.present(frame);
  if (presentation.disposition !=
      glasswyrm::output::PresentDisposition::Complete) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = presentation.error;
    return presented;
  }

  release_retired_buffers(compositor, staged);
  scene_ = std::move(candidate);
  committed_attachments_ = pending_attachments_;
  output_ = std::move(scratch);
  ++frame_ordinal_;
  last_generation_ = value.producer_generation;
  presented.ordinal = frame_ordinal_;
  presented.hash = presentation.visible_hash;
  return presented;
}

} // namespace gw::compositor
