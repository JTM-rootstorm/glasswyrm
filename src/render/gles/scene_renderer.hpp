#pragma once

#include "render/gles/context.hpp"
#include "render/scene_renderer.hpp"

#include <GLES2/gl2.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace gw::render::gles {

class GlesSceneRenderer final : public SceneRenderer {
public:
  static constexpr std::uint64_t kMaximumTextureCacheBytes =
      kMaximumGlTextureCacheBytes;

  static std::unique_ptr<GlesSceneRenderer> create(
      const ContextOptions& context_options, ContextInfo& info,
      std::uint64_t maximum_texture_bytes,
                                                    std::string& error);
  ~GlesSceneRenderer() override;
  [[nodiscard]] RenderFrameResult
  render(const RenderFrameRequest& request) override;
  void disconnect() noexcept override;

private:
  struct Texture {
    GLuint name{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t bytes{};
  };

  GlesSceneRenderer(std::unique_ptr<Context> context,
                    std::uint64_t maximum_texture_bytes)
      : context_(std::move(context)),
        maximum_texture_bytes_(maximum_texture_bytes) {}
  [[nodiscard]] bool initialize(std::string& error);
  [[nodiscard]] bool validate(const RenderFrameRequest& request,
                              RenderFrameResult& result) const;
  [[nodiscard]] bool prepare_output(const RenderFrameRequest& request,
                                    RenderFrameResult& result);
  [[nodiscard]] bool update_textures(const RenderFrameRequest& request,
                                     std::set<std::uint64_t>& new_textures,
                                     RenderFrameResult& result);
  void draw_damage(const RenderFrameRequest& request,
                   const std::set<std::uint64_t>& new_textures,
                   RenderFrameResult& result);
  void readback_damage(const RenderFrameRequest& request,
                       RenderFrameResult& result);
  void destroy_gl() noexcept;

  std::unique_ptr<Context> context_;
  std::map<std::uint64_t, Texture> textures_;
  std::vector<std::uint8_t> scratch_;
  std::uint64_t texture_bytes_{};
  std::uint64_t maximum_texture_bytes_{};
  GLuint program_{};
  GLuint framebuffer_{};
  GLuint output_texture_{};
  GLint position_{};
  GLint texcoord_{};
  GLint sampler_{};
  GLint opacity_{};
  GLint xrgb_{};
};

} // namespace gw::render::gles
