#pragma once

#include "render/gles/context.hpp"
#include "render/software/multi_output_scene_renderer.hpp"

#include <GLES2/gl2.h>

#include <map>
#include <memory>
#include <set>
#include <string>

namespace gw::render::gles {

struct OutputGlesRenderMetrics {
  std::uint64_t damage_rectangles{};
  std::uint64_t texture_uploads{};
  std::uint64_t texture_upload_bytes{};
  std::uint64_t texture_cache_bytes{};
  std::uint64_t readback_bytes{};
  std::uint8_t maximum_channel_error{};
  bool used_direct{};
  bool used_nearest{};
  bool used_bilinear{};
};

struct GlesFrameSetRenderResult {
  RenderDisposition disposition{RenderDisposition::InvalidFrame};
  glasswyrm::output::SoftwareFrameSet frames;
  std::map<std::uint64_t, OutputGlesRenderMetrics> metrics;
  std::string error;

  [[nodiscard]] bool complete() const noexcept {
    return disposition == RenderDisposition::Complete;
  }
};

class MultiOutputGlesSceneRenderer final {
public:
  static std::unique_ptr<MultiOutputGlesSceneRenderer>
  create(const ContextOptions &options, ContextInfo &info,
         std::uint64_t maximum_texture_bytes, std::string &error);
  ~MultiOutputGlesSceneRenderer();
  MultiOutputGlesSceneRenderer(const MultiOutputGlesSceneRenderer &) = delete;
  MultiOutputGlesSceneRenderer &
  operator=(const MultiOutputGlesSceneRenderer &) = delete;

  [[nodiscard]] GlesFrameSetRenderResult render(
      const software::SoftwareFrameSetRenderRequest &request);
  void disconnect() noexcept;

private:
  struct Texture {
    GLuint name{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t bytes{};
  };
  struct OutputTarget {
    GLuint texture{};
    std::uint32_t width{};
    std::uint32_t height{};
  };

  MultiOutputGlesSceneRenderer(std::unique_ptr<Context> context,
                               std::uint64_t maximum_texture_bytes)
      : context_(std::move(context)),
        maximum_texture_bytes_(maximum_texture_bytes) {}
  [[nodiscard]] bool initialize(std::string &error);
  void destroy_gl() noexcept;
  [[nodiscard]] bool update_sources(
      const software::SoftwareFrameSetRenderRequest &request,
      GlesFrameSetRenderResult &result, std::set<std::uint64_t> &updated);
  [[nodiscard]] bool render_output(
      const software::SoftwareFrameSetRenderRequest &request,
      const gwipc_output_upsert &output,
      const glasswyrm::output::OutputFrameResult *previous,
      const std::vector<std::uint64_t> &stacking,
      const std::set<std::uint64_t> &updated,
      glasswyrm::output::OutputFrameResult &frame,
      OutputGlesRenderMetrics &metrics, std::string &error);
  [[nodiscard]] bool verify_reference(
      const glasswyrm::output::SoftwareFrameSet &reference,
      GlesFrameSetRenderResult &result);

  std::unique_ptr<Context> context_;
  std::map<std::uint64_t, Texture> textures_;
  std::map<std::uint64_t, OutputTarget> targets_;
  std::vector<std::uint8_t> scratch_;
  std::uint64_t texture_bytes_{};
  std::uint64_t maximum_texture_bytes_{};
  GLuint program_{};
  GLuint framebuffer_{};
  GLint position_{};
  GLint sampler_{};
  GLint physical_extent_{};
  GLint logical_origin_{};
  GLint output_scale_{};
  GLint transform_{};
  GLint surface_rectangle_{};
  GLint clip_rectangle_{};
  GLint buffer_extent_{};
  GLint client_scale_{};
  GLint opacity_{};
  GLint xrgb_{};
};

} // namespace gw::render::gles
