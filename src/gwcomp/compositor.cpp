#include "gwcomp/compositor.hpp"

#include "render/software/renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <unistd.h>
#include <vector>

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
  return intersection(*placed,
                      Rectangle{0, 0, output.logical_width, output.logical_height});
}

} // namespace

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
    : dumper_(std::move(dump_directory)) {
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
  PresentedFrame presented;
  if (snapshot_invalid_) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    error = "complete snapshot contains duplicate surface records";
    return presented;
  }
  if (value.commit_id <= last_commit_id_ || value.producer_generation < last_generation_) {
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
  if (!staged.output || !staged.output->enabled) {
    scene_ = std::move(candidate);
    committed_attachments_ = pending_attachments_;
    last_generation_ = value.producer_generation;
    return presented;
  }

  for (const auto& [surface_id, surface] : staged.surfaces) {
    const bool metadata_only = surface.presentation_flags ==
                               GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    if (metadata_only_peer != metadata_only) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      error = metadata_only_peer ? "ProtocolServer supplied a buffered surface"
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
    if (mapping == mappings_.end() || mapping->second->width() != surface.logical_width ||
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

  DamageRegion attachment_damage(
      Rectangle{0, 0, staged.output->logical_width, staged.output->logical_height});
  for (const auto& rectangle : result.damage) attachment_damage.add(rectangle);
  for (const auto& [surface_id, surface] : staged.surfaces) {
    if (!surface.visible) continue;
    const auto old = committed_attachments_.find(surface_id);
    const auto now = pending_attachments_.find(surface_id);
    const bool changed = old == committed_attachments_.end()
                             ? now != pending_attachments_.end()
                             : now == pending_attachments_.end() || old->second != now->second;
    if (changed)
      if (const auto bounds = surface_bounds(surface, *staged.output))
        attachment_damage.add(*bounds);
  }
  result.damage = attachment_damage.rectangles();

  glasswyrm::headless::Output scratch;
  if (!scratch.configure(staged.output->output_id, staged.output->logical_width,
                         staged.output->logical_height, error)) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    return presented;
  }
  if (output_.enabled() && output_.id() == scratch.id() &&
      output_.width() == scratch.width() && output_.height() == scratch.height()) {
    std::copy(output_.pixels().begin(), output_.pixels().end(), scratch.pixels().begin());
  }
  FramebufferView framebuffer{pixel_bytes(scratch.pixels()), scratch.width(),
                              scratch.height(), scratch.width() * 4U};
  for (const auto& rectangle : result.damage) {
    if (gw::render::software::clear(framebuffer, rectangle) != RenderResult::Success) {
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
      if (surface.clipping)
        local = {surface.clip_x, surface.clip_y, surface.clip_width, surface.clip_height};
      const ImageView image{mapping->bytes(), mapping->width(), mapping->height(),
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

  glasswyrm::headless::FrameDumpMetadata metadata{
      frame_ordinal_ + 1U, value.commit_id, value.producer_generation,
      staged.output->output_id, staged.output->logical_width,
      staged.output->logical_height, static_cast<std::uint32_t>(result.damage.size())};
  glasswyrm::headless::FrameDumpResult dump;
  if (!dumper_.dump(metadata, scratch.pixels(), dump, error)) {
    presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    return presented;
  }

  for (const auto& [surface_id, old_buffer] : committed_attachments_) {
    const auto now = pending_attachments_.find(surface_id);
    if (now == pending_attachments_.end()) {
      releases_[old_buffer] = staged.surfaces.contains(surface_id)
                                  ? GWIPC_BUFFER_RELEASE_CONSUMER_DONE
                                  : GWIPC_BUFFER_RELEASE_SURFACE_REMOVED;
    } else if (now->second != old_buffer) {
      releases_[old_buffer] = GWIPC_BUFFER_RELEASE_REPLACED;
    }
  }
  for (const auto& [buffer_id, mapping] : mappings_) {
    (void)mapping;
    const bool remains_attached = std::any_of(
        pending_attachments_.begin(), pending_attachments_.end(),
        [buffer_id](const auto& item) { return item.second == buffer_id; });
    if (!remains_attached && !releases_.contains(buffer_id))
      releases_[buffer_id] = GWIPC_BUFFER_RELEASE_CONSUMER_DONE;
  }
  for (const auto& [buffer_id, reason] : releases_) {
    (void)reason;
    mappings_.erase(buffer_id);
  }
  scene_ = std::move(candidate);
  committed_attachments_ = pending_attachments_;
  output_ = std::move(scratch);
  ++frame_ordinal_;
  last_generation_ = value.producer_generation;
  presented.ordinal = frame_ordinal_;
  presented.hash = dump.fnv1a64;
  return presented;
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
