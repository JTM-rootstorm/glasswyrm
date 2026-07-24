#include "gwcomp/presentation_transaction.hpp"

#include "compositor/output_damage.hpp"
#include "render/software/multi_output_scene_renderer.hpp"

#include <algorithm>

namespace gw::compositor {

std::optional<PresentationTransaction::PreparedOutputFrame>
PresentationTransaction::prepare_output_frame_set(
    Compositor& compositor, ValidatedCommit& validated,
    const gwipc_frame_commit& value, PresentedFrame& presented,
    std::string& error) {
  const auto& staged = validated.candidate.committed();
  std::optional<PreparedVrrFrame> prepared_vrr;
  if (compositor.vrr_contract_enabled_) {
    prepared_vrr = VrrRuntime::prepare(staged, *compositor.presenter_,
                                       compositor.committed_vrr_, error);
    if (!prepared_vrr) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return std::nullopt;
    }
  }
  for (const auto& [surface_id, buffer_id] :
       compositor.pending_attachments_) {
    const auto previous = compositor.committed_attachments_.find(surface_id);
    if (previous == compositor.committed_attachments_.end() ||
        previous->second != buffer_id) {
      const auto surface = staged.surfaces.find(surface_id);
      if (surface == staged.surfaces.end())
        continue;
      auto &damage = validated.damage.surfaces[surface_id];
      damage.local_rectangles = {{0, 0, surface->second.logical_width,
                                  surface->second.logical_height}};
      damage.trusted_complete = false;
      damage.fallback_reason =
          previous == compositor.committed_attachments_.end()
              ? SurfaceDamageFallbackReason::NewBuffer
              : SurfaceDamageFallbackReason::ReplacementBuffer;
    }
  }
  const auto damage = calculate_output_damage(compositor.scene_.committed(),
                                              staged, validated.damage);
  const render::software::SoftwareFrameSetRenderRequest request{
      validated.candidate,
      compositor.mappings_,
      compositor.pending_attachments_,
      damage.regions,
      compositor.output_set_ ? &*compositor.output_set_ : nullptr,
      value.commit_id,
      value.producer_generation,
      compositor.frame_ordinal_ + 1U};
  render::OutputSceneRenderResult rendered;
  if (compositor.output_renderer_) {
    rendered = compositor.output_renderer_->render(request);
  } else {
    render::software::MultiOutputSoftwareSceneRenderer renderer;
    auto software = renderer.render(request);
    rendered = {software.disposition, std::move(software.frames), "software",
                {}, std::move(software.error), {}};
  }
  if (!rendered.complete()) {
    presented.result =
        rendered.disposition == render::RenderDisposition::InvalidBuffer
            ? GWIPC_FRAME_REJECTED_INVALID_BUFFER
            : GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    if (rendered.disposition == render::RenderDisposition::Fatal)
      presented.disposition = PresentedFrame::Disposition::Fatal;
    error = rendered.error.empty()
                ? "multi-output scene renderer rejected the frame"
                : std::move(rendered.error);
    return std::nullopt;
  }
  PreparedOutputFrame prepared;
  prepared.canonical_hash = rendered.frames.aggregate_hash();
  prepared.frame_set.emplace(std::move(rendered.frames));
  if (prepared_vrr) {
    const auto requests = prepared_vrr->presentation_requests();
    if (!prepared.frame_set->set_vrr_requests(requests, error)) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return std::nullopt;
    }
  }
  prepared.releases = calculate_retired_buffers(compositor, staged);
  if (prepared_vrr) {
    prepared.vrr_response = VrrResponseBatch::preflight(
        *prepared_vrr, value, presented.result, prepared.releases, error);
    if (!prepared.vrr_response) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return std::nullopt;
    }
    prepared.vrr = std::move(prepared_vrr);
  }
  if (validated.protocol_server && compositor.scene_manifest_) {
    prepared.manifest.emplace();
    if (!SceneManifest::prepare_output_model(
            value.commit_id, value.producer_generation, staged,
            *prepared.manifest, error)) {
      presented.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return std::nullopt;
    }
  }
  return prepared;
}

} // namespace gw::compositor
