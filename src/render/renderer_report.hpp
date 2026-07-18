#pragma once

#include "render/output_scene_renderer.hpp"
#include "render/scene_renderer.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gw::render {

struct RendererSelectionReport {
  RendererRequest requested{RendererRequest::Software};
  std::string_view selected{"software"};
  std::span<const std::string> fallback_reasons;
  std::optional<std::string> egl_platform;
  std::optional<std::string> egl_vendor;
  std::optional<std::string> egl_version;
  std::optional<std::string> gles_version;
  std::optional<std::string> gl_vendor;
  std::optional<std::string> gl_renderer;
  std::optional<std::string> gl_version;
  std::optional<std::string> gbm_device;
  std::optional<std::string> render_node;
  bool software_renderer{true};
};

class RendererReport final {
public:
  explicit RendererReport(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~RendererReport();
  RendererReport(const RendererReport&) = delete;
  RendererReport& operator=(const RendererReport&) = delete;

  [[nodiscard]] bool initialize(const RendererSelectionReport& selection,
                                std::string& error);
  [[nodiscard]] bool initialized() const noexcept { return descriptor_ >= 0; }
  [[nodiscard]] bool append_frame(const RenderFrameRequest& request,
                                  const RenderFrameResult& result,
                                  std::string_view selected,
                                  std::string& error);
  [[nodiscard]] bool append_output_frame(
      const software::SoftwareFrameSetRenderRequest& request,
      const OutputSceneRenderResult& result, std::string& error);

private:
  [[nodiscard]] bool append(std::string_view line, std::string& error);
  [[nodiscard]] bool target_is_unchanged(std::string& error) const;

  std::filesystem::path path_;
  int descriptor_{-1};
  std::uint64_t device_{};
  std::uint64_t inode_{};
};

} // namespace gw::render
