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
  for (const auto& [surface_id, buffer_id] :
       compositor.pending_attachments_) {
    const auto previous = compositor.committed_attachments_.find(surface_id);
    if (previous == compositor.committed_attachments_.end() ||
        previous->second != buffer_id)
      validated.content_changed.push_back(surface_id);
  }
  std::ranges::sort(validated.content_changed);
  validated.content_changed.erase(
      std::unique(validated.content_changed.begin(),
                  validated.content_changed.end()),
      validated.content_changed.end());
  const auto damage = calculate_output_damage(
      compositor.scene_.committed(), staged, validated.content_changed);
  const render::software::SoftwareFrameSetRenderRequest request{
      validated.candidate, compositor.mappings_,
      compositor.pending_attachments_, damage,
      compositor.output_set_ ? &*compositor.output_set_ : nullptr,
      value.commit_id, value.producer_generation,
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
  prepared.releases = calculate_retired_buffers(compositor, staged);
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
