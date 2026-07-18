#pragma once

#include "backends/output/software_frame_set.hpp"
#include "render/scene_renderer.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gw::render::software {

using PhysicalDamageMap =
    std::map<std::uint64_t, std::vector<compositor::Rectangle>>;

enum class SamplingFilter : std::uint8_t {
  Direct = 0,
  Nearest = 1,
  Bilinear = 2,
};

[[nodiscard]] SamplingFilter select_sampling_filter(
    glasswyrm::output::RationalScale output_scale,
    std::uint32_t client_buffer_scale) noexcept;

struct OutputSoftwareRenderMetrics {
  std::uint64_t damage_rectangles{};
  std::uint64_t sampled_pixels{};
  bool used_direct{};
  bool used_nearest{};
  bool used_bilinear{};
};

struct SoftwareFrameSetRenderRequest {
  const compositor::SceneModel &scene_model;
  const BufferMappingMap &mappings;
  const SurfaceAttachmentMap &attachments;
  const PhysicalDamageMap &damage;
  const glasswyrm::output::SoftwareFrameSet *previous{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t ordinal{};
};

struct SoftwareFrameSetRenderResult {
  RenderDisposition disposition{RenderDisposition::InvalidFrame};
  glasswyrm::output::SoftwareFrameSet frames;
  std::map<std::uint64_t, OutputSoftwareRenderMetrics> metrics;
  std::string error;

  [[nodiscard]] bool complete() const noexcept {
    return disposition == RenderDisposition::Complete;
  }
};

// Pure canonical CPU orchestration for a committed M13 output-model scene.
// Presentation and transaction promotion deliberately remain outside this
// layer so all output frames either finish together or are discarded together.
class MultiOutputSoftwareSceneRenderer {
public:
  [[nodiscard]] SoftwareFrameSetRenderResult
  render(const SoftwareFrameSetRenderRequest &request) const;
};

} // namespace gw::render::software
