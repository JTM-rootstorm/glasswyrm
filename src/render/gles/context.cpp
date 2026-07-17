#include "render/gles/context.hpp"

#include "config.hpp"

#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#if GW_HAS_GBM
#include <gbm.h>
#endif

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gw::render::gles {
namespace {

constexpr std::size_t kMaximumFallbackReasons = 4;
constexpr std::size_t kMaximumFallbackReasonBytes = 240;

bool contains_word(const char* words, const char* wanted) {
  if (!words) return false;
  const std::size_t length = std::strlen(wanted);
  for (const char* found = std::strstr(words, wanted); found;
       found = std::strstr(found + length, wanted)) {
    const bool left = found == words || found[-1] == ' ';
    const bool right = found[length] == '\0' || found[length] == ' ';
    if (left && right) return true;
  }
  return false;
}

void add_reason(ContextInfo& info, std::string reason) {
  if (info.fallback_reasons.size() >= kMaximumFallbackReasons) return;
  if (reason.size() > kMaximumFallbackReasonBytes)
    reason.resize(kMaximumFallbackReasonBytes);
  info.fallback_reasons.push_back(std::move(reason));
}

std::string egl_error(const char* stage) {
  return std::string(stage) + " failed with EGL error " +
         std::to_string(static_cast<unsigned>(eglGetError()));
}

const char* safe(const char* value) { return value ? value : ""; }

const char* safe(const GLubyte* value) {
  return value ? reinterpret_cast<const char*>(value) : "";
}

bool classified_software(const std::string& renderer) {
  return renderer == "llvmpipe" || renderer == "softpipe" ||
         renderer.starts_with("llvmpipe (") ||
         renderer.starts_with("softpipe (");
}

bool choose_config(EGLDisplay display, EGLint surface_type, EGLConfig& config,
                   std::string& error) {
  const EGLint attributes_with_surface[] = {
      EGL_SURFACE_TYPE, surface_type, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE};
  const EGLint attributes_without_surface[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  const EGLint* attributes =
      surface_type == EGL_DONT_CARE ? attributes_without_surface
                                    : attributes_with_surface;
  EGLint count = 0;
  if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1) {
    error = egl_error("eglChooseConfig");
    return false;
  }
  return true;
}

std::unique_ptr<Context> initialize_display(EGLDisplay display,
                                            const std::string& platform,
                                            int render_node_fd,
                                            void* gbm_device,
                                            ContextInfo& info,
                                            std::string& error) {
  if (display == EGL_NO_DISPLAY) {
    error = egl_error((platform + " eglGetPlatformDisplay").c_str());
    return {};
  }
  EGLint major = 0;
  EGLint minor = 0;
  if (!eglInitialize(display, &major, &minor)) {
    error = egl_error((platform + " eglInitialize").c_str());
    return {};
  }
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    error = egl_error("eglBindAPI");
    eglTerminate(display);
    return {};
  }

  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  const bool surfaceless =
      contains_word(extensions, "EGL_KHR_surfaceless_context");
  EGLConfig config{};
  bool configured = choose_config(
      display, surfaceless ? EGL_DONT_CARE : EGL_PBUFFER_BIT, config, error);
  if (!configured && surfaceless) {
    (void)eglGetError();
    configured = choose_config(display, EGL_PBUFFER_BIT, config, error);
  }
  if (!configured) {
    eglTerminate(display);
    return {};
  }
  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                        context_attributes);
  if (context == EGL_NO_CONTEXT) {
    error = egl_error("eglCreateContext");
    eglTerminate(display);
    return {};
  }
  EGLSurface surface = EGL_NO_SURFACE;
  if (!surfaceless) {
    const EGLint pbuffer_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, pbuffer_attributes);
    if (surface == EGL_NO_SURFACE) {
      error = egl_error("eglCreatePbufferSurface");
      eglDestroyContext(display, context);
      eglTerminate(display);
      return {};
    }
    add_reason(info, platform + " required a 1x1 pbuffer context");
  }
  if (!eglMakeCurrent(display, surface, surface, context)) {
    error = egl_error("eglMakeCurrent");
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return {};
  }

  info.platform = platform + (surface == EGL_NO_SURFACE ? "" : "+pbuffer");
  info.egl_vendor = safe(eglQueryString(display, EGL_VENDOR));
  info.egl_version = safe(eglQueryString(display, EGL_VERSION));
  info.gles_version = safe(glGetString(GL_VERSION));
  info.gl_vendor = safe(glGetString(GL_VENDOR));
  info.gl_renderer = safe(glGetString(GL_RENDERER));
  info.gl_version = info.gles_version;
  info.software_renderer = classified_software(info.gl_renderer);
  error.clear();
  return std::unique_ptr<Context>(new Context(
      display, context, surface, render_node_fd, gbm_device));
}

} // namespace

std::unique_ptr<Context> Context::create(const ContextOptions& options,
                                         ContextInfo& info,
                                         std::string& error) {
  info = {};
  const char* client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

#if GW_HAS_GBM
  if (options.render_node) {
    const int fd = ::open(options.render_node->c_str(),
                          O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    struct stat status {};
    if (fd < 0) {
      add_reason(info, "GBM render node could not be opened safely: " +
                           std::string(std::strerror(errno)));
    } else if (::fstat(fd, &status) != 0 || !S_ISCHR(status.st_mode)) {
      add_reason(info, "GBM render node is not a usable character device");
      if (fd >= 0) (void)::close(fd);
    } else if (!contains_word(client_extensions, "EGL_KHR_platform_gbm") &&
               !contains_word(client_extensions, "EGL_MESA_platform_gbm")) {
      add_reason(info, "EGL GBM platform extension is unavailable");
      (void)::close(fd);
    } else {
      gbm_device* device = gbm_create_device(fd);
      if (!device) {
        add_reason(info, "gbm_create_device failed for validated render node");
        (void)::close(fd);
      } else {
#if defined(EGL_PLATFORM_GBM_KHR)
        constexpr EGLenum platform = EGL_PLATFORM_GBM_KHR;
#else
        constexpr EGLenum platform = EGL_PLATFORM_GBM_MESA;
#endif
        std::string gbm_error;
        auto context = initialize_display(
            eglGetPlatformDisplay(platform, device, nullptr), "gbm", fd,
            device, info, gbm_error);
        if (context) {
          info.gbm_device = safe(gbm_device_get_backend_name(device));
          info.render_node = options.render_node->string();
          return context;
        }
        add_reason(info, "GBM EGL initialization failed: " + gbm_error);
        gbm_device_destroy(device);
        (void)::close(fd);
      }
    }
  } else {
    add_reason(info, "GBM context skipped because no validated render node exists");
  }
#else
  if (options.render_node)
    add_reason(info, "GBM support was unavailable at build time");
  else
    add_reason(info, "GBM context skipped because no validated render node exists");
#endif

  if (!contains_word(client_extensions, "EGL_MESA_platform_surfaceless")) {
    error = "EGL_MESA_platform_surfaceless is unavailable";
    return {};
  }
  return initialize_display(
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                            nullptr),
      "surfaceless", -1, nullptr, info, error);
}

Context::~Context() {
  if (display_ != EGL_NO_DISPLAY) {
    (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    eglTerminate(display_);
  }
#if GW_HAS_GBM
  if (gbm_device_) gbm_device_destroy(static_cast<gbm_device*>(gbm_device_));
#endif
  if (render_node_fd_ >= 0) (void)::close(render_node_fd_);
}

bool Context::make_current(std::string& error) const {
  if (eglMakeCurrent(display_, surface_, surface_, context_)) return true;
  error = egl_error("eglMakeCurrent");
  return false;
}

} // namespace gw::render::gles
