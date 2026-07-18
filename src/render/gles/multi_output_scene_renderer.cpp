#include "render/gles/multi_output_scene_renderer.hpp"

#include "compositor/scene_validation.hpp"
#include "render/software/pixel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>

namespace gw::render::gles {
namespace {

constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
void main() { gl_Position = vec4(a_position, 0.0, 1.0); }
)";

constexpr char kFragmentShader[] = R"(
precision highp float;
uniform sampler2D u_texture;
uniform vec2 u_physical_extent;
uniform vec2 u_logical_origin;
uniform vec2 u_output_scale;
uniform int u_transform;
uniform vec4 u_surface_rectangle;
uniform vec4 u_clip_rectangle;
uniform vec2 u_buffer_extent;
uniform float u_client_scale;
uniform float u_opacity;
uniform float u_xrgb;
void main() {
  vec2 native = vec2(gl_FragCoord.x,
                     u_physical_extent.y - gl_FragCoord.y);
  vec2 transformed;
  if (u_transform == 0) transformed = native;
  else if (u_transform == 1)
    transformed = vec2(native.y, u_physical_extent.x - native.x);
  else if (u_transform == 2)
    transformed = u_physical_extent - native;
  else if (u_transform == 3)
    transformed = vec2(u_physical_extent.y - native.y, native.x);
  else if (u_transform == 4)
    transformed = vec2(u_physical_extent.x - native.x, native.y);
  else if (u_transform == 5)
    transformed = vec2(u_physical_extent.y - native.y,
                       u_physical_extent.x - native.x);
  else if (u_transform == 6)
    transformed = vec2(native.x, u_physical_extent.y - native.y);
  else
    transformed = vec2(native.y, native.x);
  vec2 logical = u_logical_origin + transformed *
                 (u_output_scale.y / u_output_scale.x);
  vec2 clip_end = u_clip_rectangle.xy + u_clip_rectangle.zw;
  if (logical.x < u_clip_rectangle.x || logical.y < u_clip_rectangle.y ||
      logical.x >= clip_end.x || logical.y >= clip_end.y) discard;
  vec2 local = logical - u_surface_rectangle.xy;
  vec2 coordinates = local * u_client_scale / u_buffer_extent;
  vec4 sampled = texture2D(u_texture, coordinates).bgra;
  sampled.a = mix(sampled.a, 1.0, u_xrgb);
  gl_FragColor = sampled * u_opacity;
})";

GLuint compile_shader(const GLenum type, const char *source,
                      std::string &error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE)
    return shader;
  std::array<char, 512> log{};
  glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
  error = std::string("multi-output GLES shader compilation failed: ") +
          log.data();
  glDeleteShader(shader);
  return 0;
}

bool gl_ok(std::string &error, const char *stage) {
  const auto value = glGetError();
  if (value == GL_NO_ERROR)
    return true;
  error = std::string(stage) + " failed with GLES error " +
          std::to_string(static_cast<unsigned>(value));
  return false;
}

std::optional<compositor::Rectangle>
surface_rectangle(const gwipc_surface_upsert &surface) {
  compositor::Rectangle local{0, 0, surface.logical_width,
                              surface.logical_height};
  if (surface.clipping) {
    const auto clipped = compositor::intersection(
        local, {surface.clip_x, surface.clip_y, surface.clip_width,
                surface.clip_height});
    if (!clipped)
      return std::nullopt;
    local = *clipped;
  }
  return compositor::translate(local, surface.logical_x, surface.logical_y);
}

bool member_of(const compositor::SurfaceOutputMembership &membership,
               const std::uint64_t output_id) {
  return std::ranges::find(membership.output_ids, output_id) !=
         membership.output_ids.end();
}

bool compatible(const glasswyrm::output::OutputFrameResult *frame,
                const gwipc_output_upsert &output) {
  return frame && frame->output.output_id == output.output_id &&
         frame->output.width == output.physical_pixel_width &&
         frame->output.height == output.physical_pixel_height &&
         frame->scale == glasswyrm::output::RationalScale{
                             output.scale_numerator,
                             output.scale_denominator} &&
         frame->transform == static_cast<glasswyrm::output::OutputTransform>(
                                 output.transform);
}

std::uint8_t channel_error(const std::uint32_t left,
                           const std::uint32_t right) {
  std::uint8_t maximum = 0;
  for (const unsigned shift : {0U, 8U, 16U}) {
    const auto a = static_cast<int>((left >> shift) & 0xffU);
    const auto b = static_cast<int>((right >> shift) & 0xffU);
    maximum = std::max(maximum,
                       static_cast<std::uint8_t>(std::abs(a - b)));
  }
  return maximum;
}

void record_filter(OutputGlesRenderMetrics &metrics,
                   const software::SamplingFilter filter) {
  metrics.used_direct |= filter == software::SamplingFilter::Direct;
  metrics.used_nearest |= filter == software::SamplingFilter::Nearest;
  metrics.used_bilinear |= filter == software::SamplingFilter::Bilinear;
}

} // namespace

std::unique_ptr<MultiOutputGlesSceneRenderer>
MultiOutputGlesSceneRenderer::create(const ContextOptions &options,
                                     ContextInfo &info,
                                     const std::uint64_t maximum_texture_bytes,
                                     std::string &error) {
  if (maximum_texture_bytes == 0) {
    error = "multi-output GLES texture cache limit must be nonzero";
    return {};
  }
  auto context = Context::create(options, info, error);
  if (!context)
    return {};
  auto result = std::unique_ptr<MultiOutputGlesSceneRenderer>(
      new MultiOutputGlesSceneRenderer(std::move(context),
                                       maximum_texture_bytes));
  if (!result->initialize(error))
    return {};
  return result;
}

MultiOutputGlesSceneRenderer::~MultiOutputGlesSceneRenderer() { destroy_gl(); }

bool MultiOutputGlesSceneRenderer::initialize(std::string &error) {
  const auto vertex = compile_shader(GL_VERTEX_SHADER, kVertexShader, error);
  if (!vertex)
    return false;
  const auto fragment =
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
    error = "multi-output GLES program link failed";
    return false;
  }
  position_ = glGetAttribLocation(program_, "a_position");
  sampler_ = glGetUniformLocation(program_, "u_texture");
  physical_extent_ = glGetUniformLocation(program_, "u_physical_extent");
  logical_origin_ = glGetUniformLocation(program_, "u_logical_origin");
  output_scale_ = glGetUniformLocation(program_, "u_output_scale");
  transform_ = glGetUniformLocation(program_, "u_transform");
  surface_rectangle_ = glGetUniformLocation(program_, "u_surface_rectangle");
  clip_rectangle_ = glGetUniformLocation(program_, "u_clip_rectangle");
  buffer_extent_ = glGetUniformLocation(program_, "u_buffer_extent");
  client_scale_ = glGetUniformLocation(program_, "u_client_scale");
  opacity_ = glGetUniformLocation(program_, "u_opacity");
  xrgb_ = glGetUniformLocation(program_, "u_xrgb");
  glGenFramebuffers(1, &framebuffer_);
  return gl_ok(error, "multi-output GLES initialization");
}

void MultiOutputGlesSceneRenderer::destroy_gl() noexcept {
  if (!context_)
    return;
  std::string ignored;
  if (!context_->make_current(ignored))
    return;
  for (const auto &[id, texture] : textures_) {
    (void)id;
    glDeleteTextures(1, &texture.name);
  }
  for (const auto &[id, target] : targets_) {
    (void)id;
    glDeleteTextures(1, &target.texture);
  }
  textures_.clear();
  targets_.clear();
  texture_bytes_ = 0;
  if (framebuffer_)
    glDeleteFramebuffers(1, &framebuffer_);
  if (program_)
    glDeleteProgram(program_);
  framebuffer_ = program_ = 0;
}

void MultiOutputGlesSceneRenderer::disconnect() noexcept {
  destroy_gl();
  std::string ignored;
  (void)initialize(ignored);
}

bool MultiOutputGlesSceneRenderer::update_sources(
    const software::SoftwareFrameSetRenderRequest &request,
    GlesFrameSetRenderResult &result, std::set<std::uint64_t> &updated) {
  std::uint64_t requested = 0;
  for (const auto &[id, mapping] : request.mappings) {
    (void)id;
    if (!mapping) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "renderer attachment has no buffer mapping";
      return false;
    }
    const auto bytes = static_cast<std::uint64_t>(mapping->width()) *
                       mapping->height() * 4U;
    if (bytes > maximum_texture_bytes_ ||
        requested > maximum_texture_bytes_ - bytes) {
      result.error = "multi-output GLES texture cache limit exceeded";
      return false;
    }
    requested += bytes;
  }
  for (auto iterator = textures_.begin(); iterator != textures_.end();) {
    if (request.mappings.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    glDeleteTextures(1, &iterator->second.name);
    texture_bytes_ -= iterator->second.bytes;
    iterator = textures_.erase(iterator);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  for (const auto &[id, mapping] : request.mappings) {
    auto found = textures_.find(id);
    if (found != textures_.end() &&
        (found->second.width != mapping->width() ||
         found->second.height != mapping->height())) {
      glDeleteTextures(1, &found->second.name);
      texture_bytes_ -= found->second.bytes;
      textures_.erase(found);
      found = textures_.end();
    }
    if (found == textures_.end()) {
      Texture texture;
      texture.width = mapping->width();
      texture.height = mapping->height();
      texture.bytes = static_cast<std::uint64_t>(texture.width) *
                      texture.height * 4U;
      glGenTextures(1, &texture.name);
      found = textures_.emplace(id, texture).first;
      texture_bytes_ += texture.bytes;
    }
    const auto row_bytes = static_cast<std::size_t>(mapping->width()) * 4U;
    scratch_.resize(static_cast<std::size_t>(found->second.bytes));
    for (std::uint32_t row = 0; row < mapping->height(); ++row)
      std::memcpy(scratch_.data() + static_cast<std::size_t>(row) * row_bytes,
                  mapping->bytes().data() +
                      static_cast<std::size_t>(row) * mapping->stride(),
                  row_bytes);
    glBindTexture(GL_TEXTURE_2D, found->second.name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mapping->width(),
                 mapping->height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 scratch_.data());
    updated.insert(id);
  }
  return true;
}

bool MultiOutputGlesSceneRenderer::render_output(
    const software::SoftwareFrameSetRenderRequest &request,
    const gwipc_output_upsert &output,
    const glasswyrm::output::OutputFrameResult *previous,
    const std::vector<std::uint64_t> &stacking,
    const std::set<std::uint64_t> &updated,
    glasswyrm::output::OutputFrameResult &frame,
    OutputGlesRenderMetrics &metrics, std::string &error) {
  frame.output = {output.output_id, output.physical_pixel_width,
                  output.physical_pixel_height, output.refresh_millihertz};
  frame.scale = {output.scale_numerator, output.scale_denominator};
  frame.transform =
      static_cast<glasswyrm::output::OutputTransform>(output.transform);
  if (!frame.frame.configure(output.output_id, output.physical_pixel_width,
                             output.physical_pixel_height, error))
    return false;
  const bool copied = compatible(previous, output);
  if (copied)
    std::ranges::copy(previous->frame.pixels(), frame.frame.pixels().begin());
  const auto damage = request.damage.find(output.output_id);
  if (!copied)
    frame.damage = {{0, 0, output.physical_pixel_width,
                     output.physical_pixel_height}};
  else if (damage != request.damage.end())
    frame.damage = damage->second;
  metrics.damage_rectangles = frame.damage.size();
  if (frame.damage.size() > GWIPC_MAXIMUM_DAMAGE_RECTANGLES) {
    error = "multi-output GLES damage exceeds the rectangle limit";
    return false;
  }

  auto &target = targets_[output.output_id];
  if (target.texture && (target.width != output.physical_pixel_width ||
                         target.height != output.physical_pixel_height)) {
    glDeleteTextures(1, &target.texture);
    target = {};
  }
  if (!target.texture)
    glGenTextures(1, &target.texture);
  target.width = output.physical_pixel_width;
  target.height = output.physical_pixel_height;
  scratch_.resize(frame.frame.pixels().size() * 4U);
  for (std::uint32_t y = 0; y < target.height; ++y) {
    const auto source_y = target.height - 1U - y;
    for (std::uint32_t x = 0; x < target.width; ++x) {
      const auto pixel = frame.frame.pixels()[
          static_cast<std::size_t>(source_y) * target.width + x];
      const auto offset =
          (static_cast<std::size_t>(y) * target.width + x) * 4U;
      scratch_[offset] = static_cast<std::uint8_t>(pixel >> 16U);
      scratch_[offset + 1U] = static_cast<std::uint8_t>(pixel >> 8U);
      scratch_[offset + 2U] = static_cast<std::uint8_t>(pixel);
      scratch_[offset + 3U] = 255;
    }
  }
  glBindTexture(GL_TEXTURE_2D, target.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target.width, target.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         target.texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    error = "multi-output GLES framebuffer is incomplete";
    return false;
  }
  glViewport(0, 0, target.width, target.height);
  glUseProgram(program_);
  glUniform1i(sampler_, 0);
  glUniform2f(physical_extent_, static_cast<float>(target.width),
              static_cast<float>(target.height));
  glUniform2f(logical_origin_, static_cast<float>(output.logical_x),
              static_cast<float>(output.logical_y));
  glUniform2f(output_scale_, static_cast<float>(output.scale_numerator),
              static_cast<float>(output.scale_denominator));
  glUniform1i(transform_, static_cast<GLint>(output.transform));
  constexpr std::array<GLfloat, 8> vertices{
      -1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F};
  glEnableVertexAttribArray(static_cast<GLuint>(position_));
  glVertexAttribPointer(static_cast<GLuint>(position_), 2, GL_FLOAT, GL_FALSE,
                        0, vertices.data());
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  std::set<std::uint64_t> counted_uploads;
  for (const auto &rectangle : frame.damage) {
    if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width == 0 ||
        rectangle.height == 0 ||
        static_cast<std::uint64_t>(rectangle.x) + rectangle.width >
            target.width ||
        static_cast<std::uint64_t>(rectangle.y) + rectangle.height >
            target.height) {
      error = "multi-output GLES damage is outside its output";
      return false;
    }
    glScissor(rectangle.x, target.height - rectangle.y - rectangle.height,
              rectangle.width, rectangle.height);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    for (const auto surface_id : stacking) {
      const auto &surface = request.scene_model.committed().surfaces.at(surface_id);
      const auto membership =
          request.scene_model.committed().surface_outputs.find(surface_id);
      if (!surface.visible || surface.opacity == 0 ||
          surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY ||
          membership == request.scene_model.committed().surface_outputs.end() ||
          !member_of(membership->second, output.output_id))
        continue;
      const auto clip = surface_rectangle(surface);
      if (!clip)
        continue;
      const auto attachment = request.attachments.find(surface_id);
      if (attachment == request.attachments.end() ||
          !request.mappings.contains(attachment->second) ||
          !textures_.contains(attachment->second)) {
        error = "visible surface has no GLES texture attachment";
        return false;
      }
      const auto &mapping = *request.mappings.at(attachment->second);
      const auto expected_width =
          static_cast<std::uint64_t>(surface.logical_width) *
          surface.scale_numerator;
      const auto expected_height =
          static_cast<std::uint64_t>(surface.logical_height) *
          surface.scale_numerator;
      if (mapping.width() != expected_width ||
          mapping.height() != expected_height) {
        error = "surface buffer dimensions do not match its client scale";
        return false;
      }
      const auto filter = software::select_sampling_filter(
          frame.scale, surface.scale_numerator);
      record_filter(metrics, filter);
      const auto texture = textures_.at(attachment->second).name;
      glBindTexture(GL_TEXTURE_2D, texture);
      const auto gl_filter = filter == software::SamplingFilter::Bilinear
                                 ? GL_LINEAR
                                 : GL_NEAREST;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
      glUniform4f(surface_rectangle_, static_cast<float>(surface.logical_x),
                  static_cast<float>(surface.logical_y),
                  static_cast<float>(surface.logical_width),
                  static_cast<float>(surface.logical_height));
      glUniform4f(clip_rectangle_, static_cast<float>(clip->x),
                  static_cast<float>(clip->y), static_cast<float>(clip->width),
                  static_cast<float>(clip->height));
      glUniform2f(buffer_extent_, static_cast<float>(mapping.width()),
                  static_cast<float>(mapping.height()));
      glUniform1f(client_scale_, static_cast<float>(surface.scale_numerator));
      glUniform1f(opacity_, static_cast<float>(surface.opacity) / 65536.0F);
      glUniform1f(xrgb_, mapping.pixel_format() == GWIPC_PIXEL_FORMAT_XRGB8888
                             ? 1.0F
                             : 0.0F);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      if (updated.contains(attachment->second) &&
          counted_uploads.insert(attachment->second).second) {
        ++metrics.texture_uploads;
        metrics.texture_upload_bytes += textures_.at(attachment->second).bytes;
      }
    }
    scratch_.resize(static_cast<std::size_t>(rectangle.width) *
                    rectangle.height * 4U);
    glReadPixels(rectangle.x,
                 target.height - rectangle.y - rectangle.height,
                 rectangle.width, rectangle.height, GL_RGBA, GL_UNSIGNED_BYTE,
                 scratch_.data());
    metrics.readback_bytes += scratch_.size();
    for (std::uint32_t y = 0; y < rectangle.height; ++y) {
      const auto source_y = rectangle.height - 1U - y;
      for (std::uint32_t x = 0; x < rectangle.width; ++x) {
        const auto source =
            (static_cast<std::size_t>(source_y) * rectangle.width + x) * 4U;
        frame.frame.pixels()[
            static_cast<std::size_t>(rectangle.y + y) * target.width +
            rectangle.x + x] =
            UINT32_C(0xff000000) |
            (static_cast<std::uint32_t>(scratch_[source]) << 16U) |
            (static_cast<std::uint32_t>(scratch_[source + 1U]) << 8U) |
            scratch_[source + 2U];
      }
    }
  }
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  metrics.texture_cache_bytes = texture_bytes_;
  return gl_ok(error, "multi-output GLES rendering");
}

bool MultiOutputGlesSceneRenderer::verify_reference(
    const glasswyrm::output::SoftwareFrameSet &reference,
    GlesFrameSetRenderResult &result) {
  for (const auto &[output_id, output] : result.frames.outputs()) {
    const auto expected = reference.outputs().find(output_id);
    if (expected == reference.outputs().end() ||
        expected->second.frame.pixels().size() != output.frame.pixels().size()) {
      result.error = "GLES output set differs from the software reference";
      return false;
    }
    auto &metrics = result.metrics.at(output_id);
    for (std::size_t index = 0; index < output.frame.pixels().size(); ++index)
      metrics.maximum_channel_error = std::max(
          metrics.maximum_channel_error,
          channel_error(output.frame.pixels()[index],
                        expected->second.frame.pixels()[index]));
    const auto permitted = metrics.used_bilinear ? 1U : 0U;
    if (metrics.maximum_channel_error > permitted) {
      result.error = "GLES pixels exceed the software-reference tolerance";
      return false;
    }
  }
  return true;
}

GlesFrameSetRenderResult MultiOutputGlesSceneRenderer::render(
    const software::SoftwareFrameSetRenderRequest &request) {
  GlesFrameSetRenderResult result;
  software::MultiOutputSoftwareSceneRenderer canonical_renderer;
  auto canonical = canonical_renderer.render(request);
  if (!canonical.complete()) {
    result.disposition = canonical.disposition;
    result.error = std::move(canonical.error);
    return result;
  }
  if (!context_->make_current(result.error)) {
    result.disposition = RenderDisposition::Fatal;
    return result;
  }
  std::set<std::uint64_t> updated;
  if (!update_sources(request, result, updated))
    return result;
  std::set<std::uint64_t> active_outputs;
  glasswyrm::output::SoftwareFrameSet staged;
  const auto stacking = request.scene_model.stacking_order();
  for (const auto &[output_id, output] : request.scene_model.committed().outputs) {
    if (!output.enabled)
      continue;
    active_outputs.insert(output_id);
    const glasswyrm::output::OutputFrameResult *previous = nullptr;
    if (request.previous) {
      const auto found = request.previous->outputs().find(output_id);
      if (found != request.previous->outputs().end())
        previous = &found->second;
    }
    glasswyrm::output::OutputFrameResult frame;
    auto &metrics = result.metrics[output_id];
    if (!render_output(request, output, previous, stacking, updated, frame,
                       metrics, result.error) ||
        !staged.append(std::move(frame), result.error)) {
      result.disposition = RenderDisposition::Fatal;
      return result;
    }
  }
  for (auto iterator = targets_.begin(); iterator != targets_.end();) {
    if (active_outputs.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    glDeleteTextures(1, &iterator->second.texture);
    iterator = targets_.erase(iterator);
  }
  const auto &scene = request.scene_model.committed();
  if (!staged.finalize(scene.configuration_generation, scene.primary_output_id,
                       request.commit_id, request.generation, request.ordinal,
                       result.error)) {
    result.disposition = RenderDisposition::Fatal;
    return result;
  }
  result.frames = std::move(staged);
  if (!verify_reference(canonical.frames, result)) {
    result.disposition = RenderDisposition::Fatal;
    return result;
  }
  result.disposition = RenderDisposition::Complete;
  result.error.clear();
  return result;
}

} // namespace gw::render::gles
