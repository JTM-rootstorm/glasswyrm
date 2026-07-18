#pragma once

#include "render/software/multi_output_scene_renderer.hpp"

#include <memory>
#include <string>

namespace gw::render {

struct OutputSceneRenderResult {
  RenderDisposition disposition{RenderDisposition::InvalidFrame};
  glasswyrm::output::SoftwareFrameSet frames;
  std::string selected_renderer;
  std::string fallback_reason;
  std::string error;

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
