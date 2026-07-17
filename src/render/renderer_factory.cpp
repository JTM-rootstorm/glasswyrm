#include "render/renderer_factory.hpp"

#include "render/renderer_report.hpp"
#include "render/software/scene_renderer.hpp"
#include "config.hpp"
#if GW_HAS_GLES_RENDERER
#include "render/gles/scene_renderer.hpp"
#endif

#include <utility>
#include <vector>

namespace gw::render {
namespace {

class ReportingSceneRenderer final : public SceneRenderer {
public:
  ReportingSceneRenderer(std::unique_ptr<SceneRenderer> renderer,
                         std::unique_ptr<RendererReport> report,
                         std::string selected)
      : renderer_(std::move(renderer)), report_(std::move(report)),
        selected_(std::move(selected)) {}

  RenderFrameResult render(const RenderFrameRequest& request) override {
    auto result = renderer_->render(request);
    std::string report_error;
    if (!report_->append_frame(request, result, selected_, report_error)) {
      result.disposition = RenderDisposition::Fatal;
      result.error = std::move(report_error);
    }
    return result;
  }

  void disconnect() noexcept override { renderer_->disconnect(); }

private:
  std::unique_ptr<SceneRenderer> renderer_;
  std::unique_ptr<RendererReport> report_;
  std::string selected_;
};

} // namespace

const char* renderer_request_name(const RendererRequest request) noexcept {
  switch (request) {
    case RendererRequest::Software: return "software";
    case RendererRequest::Gles: return "gles";
    case RendererRequest::Auto: return "auto";
  }
  return "software";
}

bool create_scene_renderer(
    const RendererRequest requested,
    const std::optional<std::filesystem::path>& report_path,
    std::unique_ptr<SceneRenderer>& renderer, std::string& error) {
  renderer.reset();
  error.clear();
  std::vector<std::string> fallback_reasons;
  std::unique_ptr<SceneRenderer> selected;
  std::string selected_name = "software";
  RendererSelectionReport selection;
  selection.requested = requested;
#if GW_HAS_GLES_RENDERER
  gles::ContextInfo context_info;
  std::string gles_error;
  if (requested != RendererRequest::Software) {
    selected = gles::GlesSceneRenderer::create(context_info, gles_error);
    if (selected) {
      selected_name = "gles";
      fallback_reasons = context_info.fallback_reasons;
      selection.egl_platform = context_info.platform;
      selection.egl_vendor = context_info.egl_vendor;
      selection.egl_version = context_info.egl_version;
      selection.gles_version = context_info.gles_version;
      selection.gl_vendor = context_info.gl_vendor;
      selection.gl_renderer = context_info.gl_renderer;
      selection.gl_version = context_info.gl_version;
      selection.software_renderer = context_info.software_renderer;
    } else if (requested == RendererRequest::Gles) {
      error = std::move(gles_error);
      return false;
    } else {
      fallback_reasons.emplace_back(std::move(gles_error));
    }
  }
#else
  if (requested == RendererRequest::Gles) {
    error = "GLES renderer was not enabled at build time";
    return false;
  }
  if (requested == RendererRequest::Auto)
    fallback_reasons.emplace_back("GLES renderer was not enabled at build time");
#endif
  if (!selected)
    selected = std::make_unique<software::SoftwareSceneRenderer>();
  if (!report_path) {
    renderer = std::move(selected);
    return true;
  }

  auto report = std::make_unique<RendererReport>(*report_path);
  selection.selected = selected_name;
  selection.fallback_reasons = fallback_reasons;
  if (!report->initialize(selection, error)) return false;
  renderer = std::make_unique<ReportingSceneRenderer>(
      std::move(selected), std::move(report), selected_name);
  return true;
}

} // namespace gw::render
