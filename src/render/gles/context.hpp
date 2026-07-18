#pragma once

#include <EGL/egl.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gw::render::gles {

struct ContextInfo {
  std::string platform;
  std::string egl_vendor;
  std::string egl_version;
  std::string gles_version;
  std::string gl_vendor;
  std::string gl_renderer;
  std::string gl_version;
  std::optional<std::string> gbm_device;
  std::optional<std::string> render_node;
  bool software_renderer{};
  std::vector<std::string> fallback_reasons;
};

struct ContextOptions {
  std::optional<std::filesystem::path> render_node;
};

class Context final {
public:
  static std::unique_ptr<Context> create(const ContextOptions& options,
                                         ContextInfo& info,
                                         std::string& error);
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  [[nodiscard]] bool make_current(std::string& error) const;

  // Public only for the translation-unit context construction helper.
  Context(EGLDisplay display, EGLContext context, EGLSurface surface,
          int render_node_fd, void* gbm_device)
      : display_(display), context_(context), surface_(surface),
        render_node_fd_(render_node_fd), gbm_device_(gbm_device) {}

private:

  EGLDisplay display_{EGL_NO_DISPLAY};
  EGLContext context_{EGL_NO_CONTEXT};
  EGLSurface surface_{EGL_NO_SURFACE};
  int render_node_fd_{-1};
  void* gbm_device_{};
};

} // namespace gw::render::gles
