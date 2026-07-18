#include "render/gles/scene_renderer.hpp"

#include <array>
#include <set>

namespace gw::render::gles {
namespace {

constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_texcoord = a_texcoord;
})";

constexpr char kFragmentShader[] = R"(
precision mediump float;
uniform sampler2D u_texture;
uniform float u_opacity;
uniform float u_xrgb;
varying vec2 v_texcoord;
void main() {
  vec4 sampled = texture2D(u_texture, v_texcoord).bgra;
  sampled.a = mix(sampled.a, 1.0, u_xrgb);
  gl_FragColor = sampled * u_opacity;
})";

GLuint compile_shader(const GLenum type, const char* source,
                      std::string& error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) return shader;
  std::array<char, 512> log{};
  glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
  error = std::string("GLES shader compilation failed: ") + log.data();
  glDeleteShader(shader);
  return 0;
}

bool gl_ok(std::string& error, const char* stage) {
  const GLenum value = glGetError();
  if (value == GL_NO_ERROR) return true;
  error = std::string(stage) + " failed with GLES error " +
          std::to_string(static_cast<unsigned>(value));
  return false;
}

} // namespace

std::unique_ptr<GlesSceneRenderer>
GlesSceneRenderer::create(const ContextOptions& context_options,
                          ContextInfo& info,
                          const std::uint64_t maximum_texture_bytes,
                          std::string& error) {
  if (maximum_texture_bytes == 0) {
    error = "GLES texture cache limit must be nonzero";
    return {};
  }
  auto context = Context::create(context_options, info, error);
  if (!context) return {};
  auto renderer = std::unique_ptr<GlesSceneRenderer>(
      new GlesSceneRenderer(std::move(context), maximum_texture_bytes));
  if (!renderer->initialize(error)) return {};
  return renderer;
}

GlesSceneRenderer::~GlesSceneRenderer() { destroy_gl(); }

bool GlesSceneRenderer::initialize(std::string& error) {
  const GLuint vertex = compile_shader(GL_VERTEX_SHADER, kVertexShader, error);
  if (!vertex) return false;
  const GLuint fragment =
      compile_shader(GL_FRAGMENT_SHADER, kFragmentShader, error);
  if (!fragment) {
    glDeleteShader(vertex);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertex);
  glAttachShader(program_, fragment);
  glLinkProgram(program_);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    error = "GLES shader program link failed";
    return false;
  }
  position_ = glGetAttribLocation(program_, "a_position");
  texcoord_ = glGetAttribLocation(program_, "a_texcoord");
  sampler_ = glGetUniformLocation(program_, "u_texture");
  opacity_ = glGetUniformLocation(program_, "u_opacity");
  xrgb_ = glGetUniformLocation(program_, "u_xrgb");
  glGenFramebuffers(1, &framebuffer_);
  glGenTextures(1, &output_texture_);
  return gl_ok(error, "GLES renderer initialization");
}

void GlesSceneRenderer::destroy_gl() noexcept {
  if (!context_) return;
  std::string ignored;
  if (!context_->make_current(ignored)) return;
  for (const auto& [id, texture] : textures_) {
    (void)id;
    glDeleteTextures(1, &texture.name);
  }
  textures_.clear();
  texture_bytes_ = 0;
  if (output_texture_) glDeleteTextures(1, &output_texture_);
  if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
  if (program_) glDeleteProgram(program_);
  output_texture_ = framebuffer_ = program_ = 0;
}

void GlesSceneRenderer::disconnect() noexcept {
  destroy_gl();
  std::string ignored;
  (void)initialize(ignored);
}

RenderFrameResult GlesSceneRenderer::render(const RenderFrameRequest& request) {
  RenderFrameResult result;
  result.selected_renderer = "gles";
  result.metrics.damage_rectangles = request.damage.size();
  if (!validate(request, result)) return result;
  if (!context_->make_current(result.error)) {
    result.disposition = RenderDisposition::Fatal;
    return result;
  }
  std::set<std::uint64_t> new_textures;
  if (!prepare_output(request, result) ||
      !update_textures(request, new_textures, result))
    return result;
  draw_damage(request, new_textures, result);
  readback_damage(request, result);
  if (!gl_ok(result.error, "GLES scene rendering")) {
    result.disposition = RenderDisposition::Fatal;
    return result;
  }
  result.disposition = RenderDisposition::Complete;
  return result;
}

} // namespace gw::render::gles
