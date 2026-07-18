#include "render/renderer_factory.hpp"

#include "render/renderer_report.hpp"
#include "render/software/scene_renderer.hpp"
#include "config.hpp"
#if GW_HAS_GLES_RENDERER
#include "render/gles/multi_output_scene_renderer.hpp"
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
    const std::string_view selected = result.selected_renderer.empty()
                                          ? std::string_view(selected_)
                                          : result.selected_renderer;
    if (!report_->append_frame(request, result, selected, report_error)) {
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

#if GW_HAS_GLES_RENDERER
class AutoSceneRenderer final : public SceneRenderer {
public:
  explicit AutoSceneRenderer(std::unique_ptr<SceneRenderer> gles)
      : gles_(std::move(gles)),
        software_(std::make_unique<software::SoftwareSceneRenderer>()) {}

  RenderFrameResult render(const RenderFrameRequest& request) override {
    auto result = gles_->render(request);
    if (result.disposition != RenderDisposition::InvalidFrame) return result;
    std::string reason = std::move(result.error);
    if (reason.size() > 240) reason.resize(240);
    result = software_->render(request);
    result.selected_renderer = "software";
    result.fallback_reason = std::move(reason);
    return result;
  }

  void disconnect() noexcept override {
    gles_->disconnect();
    software_->disconnect();
  }

private:
  std::unique_ptr<SceneRenderer> gles_;
  std::unique_ptr<SceneRenderer> software_;
};

class GlesOutputSceneRenderer final : public OutputSceneRenderer {
public:
  explicit GlesOutputSceneRenderer(
      std::unique_ptr<gles::MultiOutputGlesSceneRenderer> renderer)
      : renderer_(std::move(renderer)) {}

  OutputSceneRenderResult render(
      const software::SoftwareFrameSetRenderRequest& request) override {
    auto rendered = renderer_->render(request);
    return {rendered.disposition, std::move(rendered.frames), "gles", {},
            std::move(rendered.error)};
  }

  void disconnect() noexcept override { renderer_->disconnect(); }

private:
  std::unique_ptr<gles::MultiOutputGlesSceneRenderer> renderer_;
};
#endif

class SoftwareOutputSceneRenderer final : public OutputSceneRenderer {
public:
  OutputSceneRenderResult render(
      const software::SoftwareFrameSetRenderRequest& request) override {
    auto rendered = renderer_.render(request);
    return {rendered.disposition, std::move(rendered.frames), "software", {},
            std::move(rendered.error)};
  }

  void disconnect() noexcept override {}

private:
  software::MultiOutputSoftwareSceneRenderer renderer_;
};

#if GW_HAS_GLES_RENDERER
class AutoOutputSceneRenderer final : public OutputSceneRenderer {
public:
  explicit AutoOutputSceneRenderer(
      std::unique_ptr<OutputSceneRenderer> accelerated)
      : accelerated_(std::move(accelerated)) {}

  OutputSceneRenderResult render(
      const software::SoftwareFrameSetRenderRequest& request) override {
    auto rendered = accelerated_->render(request);
    if (rendered.disposition != RenderDisposition::InvalidFrame)
      return rendered;
    auto reason = std::move(rendered.error);
    if (reason.size() > 240)
      reason.resize(240);
    rendered = software_.render(request);
    rendered.fallback_reason = std::move(reason);
    return rendered;
  }

  void disconnect() noexcept override {
    accelerated_->disconnect();
    software_.disconnect();
  }

private:
  std::unique_ptr<OutputSceneRenderer> accelerated_;
  SoftwareOutputSceneRenderer software_;
};
#endif

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
  return create_scene_renderer({requested, report_path, std::nullopt,
                                kMaximumGlTextureCacheBytes},
                               renderer, error);
}

bool create_scene_renderer(
    const RendererCreateOptions& options,
    std::unique_ptr<SceneRenderer>& renderer, std::string& error) {
  renderer.reset();
  error.clear();
  std::vector<std::string> fallback_reasons;
  std::unique_ptr<SceneRenderer> selected;
  std::string selected_name = "software";
  RendererSelectionReport selection;
  selection.requested = options.requested;
#if GW_HAS_GLES_RENDERER
  gles::ContextInfo context_info;
  std::string gles_error;
  if (options.requested != RendererRequest::Software) {
    const gles::ContextOptions context_options{options.render_node};
    selected = gles::GlesSceneRenderer::create(
        context_options, context_info, options.maximum_texture_bytes,
        gles_error);
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
      selection.gbm_device = context_info.gbm_device;
      selection.render_node = context_info.render_node;
      selection.software_renderer = context_info.software_renderer;
      if (options.requested == RendererRequest::Auto)
        selected = std::make_unique<AutoSceneRenderer>(std::move(selected));
    } else if (options.requested == RendererRequest::Gles) {
      error = std::move(gles_error);
      return false;
    } else {
      fallback_reasons = context_info.fallback_reasons;
      if (fallback_reasons.size() == 4) fallback_reasons.pop_back();
      if (gles_error.size() > 240) gles_error.resize(240);
      fallback_reasons.emplace_back(std::move(gles_error));
    }
  }
#else
  if (options.requested == RendererRequest::Gles) {
    error = "GLES renderer was not enabled at build time";
    return false;
  }
  if (options.requested == RendererRequest::Auto)
    fallback_reasons.emplace_back("GLES renderer was not enabled at build time");
#endif
  if (!selected)
    selected = std::make_unique<software::SoftwareSceneRenderer>();
  if (!options.report_path) {
    renderer = std::move(selected);
    return true;
  }

  auto report = std::make_unique<RendererReport>(*options.report_path);
  selection.selected = selected_name;
  selection.fallback_reasons = fallback_reasons;
  if (!report->initialize(selection, error)) return false;
  renderer = std::make_unique<ReportingSceneRenderer>(
      std::move(selected), std::move(report), selected_name);
  return true;
}

bool create_output_scene_renderer(
    const RendererCreateOptions& options,
    std::unique_ptr<OutputSceneRenderer>& renderer, std::string& error) {
  renderer.reset();
  error.clear();
#if GW_HAS_GLES_RENDERER
  if (options.requested != RendererRequest::Software) {
    gles::ContextInfo context_info;
    auto gles_renderer = gles::MultiOutputGlesSceneRenderer::create(
        {options.render_node}, context_info, options.maximum_texture_bytes,
        error);
    if (gles_renderer) {
      renderer = std::make_unique<GlesOutputSceneRenderer>(
          std::move(gles_renderer));
      if (options.requested == RendererRequest::Auto)
        renderer =
            std::make_unique<AutoOutputSceneRenderer>(std::move(renderer));
      return true;
    }
    if (options.requested == RendererRequest::Gles)
      return false;
    error.clear();
  }
#else
  if (options.requested == RendererRequest::Gles) {
    error = "GLES renderer was not enabled at build time";
    return false;
  }
#endif
  renderer = std::make_unique<SoftwareOutputSceneRenderer>();
  return true;
}

} // namespace gw::render
