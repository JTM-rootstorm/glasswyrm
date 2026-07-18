#pragma once

#include "render/software/multi_output_scene_renderer.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gw::render {

struct OutputRendererMetrics {
  std::uint64_t texture_uploads{};
  std::uint64_t texture_upload_bytes{};
  std::vector<compositor::Rectangle> physical_damage_rectangles;
  std::uint64_t readback_bytes{};
  std::uint64_t texture_cache_bytes{};
  glasswyrm::output::RationalScale scale;
  glasswyrm::output::OutputTransform transform{
      glasswyrm::output::OutputTransform::Normal};
  std::string fallback_reason;
  std::uint8_t maximum_fractional_comparison_error{};
  bool used_direct{};
  bool used_nearest{};
  bool used_bilinear{};
};

struct OutputSceneRenderResult {
  RenderDisposition disposition{RenderDisposition::InvalidFrame};
  glasswyrm::output::SoftwareFrameSet frames;
  std::string selected_renderer;
  std::string fallback_reason;
  std::string error;
  std::map<std::uint64_t, OutputRendererMetrics> metrics;

  [[nodiscard]] bool complete() const noexcept {
    return disposition == RenderDisposition::Complete;
  }
};

class OutputSceneRenderer {
public:
  virtual ~OutputSceneRenderer() = default;
  [[nodiscard]] virtual OutputSceneRenderResult render(
      const software::SoftwareFrameSetRenderRequest& request) = 0;
  virtual void disconnect() noexcept = 0;
};

} // namespace gw::render
