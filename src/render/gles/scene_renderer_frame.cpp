#include "render/gles/scene_renderer.hpp"

#include "render/software/pixel.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace gw::render::gles {
namespace {

std::optional<compositor::Rectangle>
surface_local_rectangle(const gwipc_surface_upsert& surface) {
  compositor::Rectangle local{0, 0, surface.logical_width,
                              surface.logical_height};
  if (!surface.clipping) return local;
  return compositor::intersection(
      local, {surface.clip_x, surface.clip_y, surface.clip_width,
              surface.clip_height});
}

std::optional<compositor::Rectangle>
surface_rectangle(const gwipc_surface_upsert& surface) {
  const auto local = surface_local_rectangle(surface);
  return local ? compositor::translate(*local, surface.logical_x,
                                       surface.logical_y)
               : std::nullopt;
}

bool premultiplied(const compositor::BufferMapping& mapping) {
  if (mapping.pixel_format() != GWIPC_PIXEL_FORMAT_ARGB8888) return true;
  for (std::uint32_t y = 0; y < mapping.height(); ++y) {
    for (std::uint32_t x = 0; x < mapping.width(); ++x) {
      const auto offset = static_cast<std::size_t>(y) * mapping.stride() +
                          static_cast<std::size_t>(x) * 4U;
      const auto word = software::load_u32(mapping.bytes().data() + offset);
      if (!software::is_premultiplied(software::unpack_argb8888(word)))
        return false;
    }
  }
  return true;
}

} // namespace

bool GlesSceneRenderer::validate(const RenderFrameRequest& request,
                                 RenderFrameResult& result) const {
  if (!request.scene.output || !request.scene.output->enabled) {
    result.error = "renderer requires one enabled output";
    return false;
  }
  const auto& output = *request.scene.output;
  if (output.transform != GWIPC_TRANSFORM_NORMAL ||
      output.scale_numerator != 1 || output.scale_denominator != 1) {
    result.error = "GLES supports only normal-transform scale-1 outputs";
    return false;
  }
  std::uint64_t requested_bytes = 0;
  for (const auto& [id, mapping] : request.mappings) {
    (void)id;
    if (!mapping) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "renderer attachment has no buffer mapping";
      return false;
    }
    const std::uint64_t width = mapping->width();
    if (width > std::numeric_limits<std::uint64_t>::max() / 4U ||
        mapping->height() >
            std::numeric_limits<std::uint64_t>::max() / (width * 4U)) {
      result.error = "GLES texture size overflows accounting";
      return false;
    }
    const std::uint64_t bytes = width * mapping->height() * 4U;
    if (bytes > maximum_texture_bytes_ ||
        requested_bytes > maximum_texture_bytes_ - bytes) {
      result.error = "GLES texture cache limit exceeded";
      return false;
    }
    requested_bytes += bytes;
  }
  for (const auto surface_id : request.stacking_order) {
    const auto found = request.scene.surfaces.find(surface_id);
    if (found == request.scene.surfaces.end()) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "renderer stacking order is invalid";
      return false;
    }
    const auto& surface = found->second;
    if (!surface.visible || surface.opacity == 0 ||
        surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
      continue;
    if (surface.transform != GWIPC_TRANSFORM_NORMAL ||
        surface.scale_numerator != 1 || surface.scale_denominator != 1) {
      result.error = "GLES supports only normal-transform scale-1 surfaces";
      return false;
    }
    const auto attachment = request.attachments.find(surface_id);
    if (attachment == request.attachments.end()) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "visible surface has no renderer attachment";
      return false;
    }
    const auto mapping = request.mappings.find(attachment->second);
    if (mapping == request.mappings.end() || !mapping->second) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "renderer attachment has no buffer mapping";
      return false;
    }
    const auto local = surface_local_rectangle(surface);
    if (!local || local->x < 0 || local->y < 0 ||
        static_cast<std::uint64_t>(local->x) + local->width >
            mapping->second->width() ||
        static_cast<std::uint64_t>(local->y) + local->height >
            mapping->second->height()) {
      result.error = "GLES surface sampling rectangle is unsupported";
      return false;
    }
    if (!premultiplied(*mapping->second)) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "ARGB buffer contains a non-premultiplied pixel";
      return false;
    }
  }
  for (const auto& damage : request.damage) {
    if (!compositor::has_valid_extents(damage) || damage.x < 0 ||
        damage.y < 0 ||
        static_cast<std::uint64_t>(damage.x) + damage.width >
            output.logical_width ||
        static_cast<std::uint64_t>(damage.y) + damage.height >
            output.logical_height) {
      result.disposition = RenderDisposition::InvalidBuffer;
      result.error = "framebuffer damage is invalid";
      return false;
    }
  }
  return true;
}

bool GlesSceneRenderer::prepare_output(const RenderFrameRequest& request,
                                       RenderFrameResult& result) {
  const auto& output = *request.scene.output;
  if (!result.frame.configure(output.output_id, output.logical_width,
                              output.logical_height, result.error))
    return false;
  if (request.previous && request.previous->enabled() &&
      request.previous->id() == result.frame.id() &&
      request.previous->width() == result.frame.width() &&
      request.previous->height() == result.frame.height()) {
    std::copy(request.previous->pixels().begin(), request.previous->pixels().end(),
              result.frame.pixels().begin());
  }
  scratch_.resize(result.frame.pixels().size() * 4U);
  for (std::size_t index = 0; index < result.frame.pixels().size(); ++index) {
    const auto pixel = result.frame.pixels()[index];
    scratch_[index * 4U] = static_cast<std::uint8_t>(pixel >> 16U);
    scratch_[index * 4U + 1U] = static_cast<std::uint8_t>(pixel >> 8U);
    scratch_[index * 4U + 2U] = static_cast<std::uint8_t>(pixel);
    scratch_[index * 4U + 3U] = 255;
  }
  glBindTexture(GL_TEXTURE_2D, output_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, output.logical_width,
               output.logical_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               scratch_.data());
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output_texture_, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    return true;
  result.disposition = RenderDisposition::Fatal;
  result.error = "GLES output framebuffer is incomplete";
  return false;
}

bool GlesSceneRenderer::update_textures(
    const RenderFrameRequest& request, std::set<std::uint64_t>& new_textures,
    RenderFrameResult& result) {
  std::set<std::uint64_t> active;
  for (const auto& [id, mapping] : request.mappings) {
    (void)mapping;
    active.insert(id);
  }
  for (auto it = textures_.begin(); it != textures_.end();) {
    if (active.contains(it->first)) {
      ++it;
      continue;
    }
    glDeleteTextures(1, &it->second.name);
    texture_bytes_ -= it->second.bytes;
    it = textures_.erase(it);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  for (const auto& [id, mapping] : request.mappings) {
    auto found = textures_.find(id);
    if (found != textures_.end() &&
        (found->second.width != mapping->width() ||
         found->second.height != mapping->height())) {
      glDeleteTextures(1, &found->second.name);
      texture_bytes_ -= found->second.bytes;
      textures_.erase(found);
      found = textures_.end();
    }
    if (found != textures_.end()) continue;
    auto& texture = textures_[id];
    texture.width = mapping->width();
    texture.height = mapping->height();
    texture.bytes = static_cast<std::uint64_t>(texture.width) *
                    texture.height * 4U;
    glGenTextures(1, &texture.name);
    glBindTexture(GL_TEXTURE_2D, texture.name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const std::size_t row_bytes = static_cast<std::size_t>(texture.width) * 4U;
    scratch_.resize(static_cast<std::size_t>(texture.bytes));
    for (std::uint32_t row = 0; row < texture.height; ++row)
      std::memcpy(scratch_.data() + static_cast<std::size_t>(row) * row_bytes,
                  mapping->bytes().data() +
                      static_cast<std::size_t>(row) * mapping->stride(),
                  row_bytes);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
    texture_bytes_ += texture.bytes;
    new_textures.insert(id);
    ++result.metrics.texture_uploads;
    result.metrics.texture_upload_bytes += texture.bytes;
  }
  result.metrics.texture_cache_bytes = texture_bytes_;
  return true;
}

void GlesSceneRenderer::draw_damage(
    const RenderFrameRequest& request,
    const std::set<std::uint64_t>& new_textures, RenderFrameResult& result) {
  const auto& output = *request.scene.output;
  glViewport(0, 0, output.logical_width, output.logical_height);
  glUseProgram(program_);
  glUniform1i(sampler_, 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  for (const auto& damage : request.damage) {
    glScissor(damage.x, output.logical_height - damage.y - damage.height,
              damage.width, damage.height);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    for (const auto surface_id : request.stacking_order) {
      const auto& surface = request.scene.surfaces.at(surface_id);
      if (!surface.visible || surface.opacity == 0 ||
          surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
        continue;
      const auto bounds = surface_rectangle(surface);
      const auto painted = bounds ? compositor::intersection(*bounds, damage)
                                  : std::nullopt;
      if (!painted) continue;
      const auto buffer_id = request.attachments.at(surface_id);
      const auto mapping = request.mappings.at(buffer_id);
      if (!new_textures.contains(buffer_id)) {
        const auto source_x = static_cast<std::uint32_t>(
            painted->x - surface.logical_x);
        const auto source_y = static_cast<std::uint32_t>(
            painted->y - surface.logical_y);
        const std::size_t row_bytes =
            static_cast<std::size_t>(painted->width) * 4U;
        scratch_.resize(row_bytes * painted->height);
        for (std::uint32_t row = 0; row < painted->height; ++row) {
          const auto source_offset =
              static_cast<std::size_t>(source_y + row) * mapping->stride() +
              static_cast<std::size_t>(source_x) * 4U;
          std::memcpy(scratch_.data() + static_cast<std::size_t>(row) * row_bytes,
                      mapping->bytes().data() + source_offset, row_bytes);
        }
        glBindTexture(GL_TEXTURE_2D, textures_.at(buffer_id).name);
        glTexSubImage2D(GL_TEXTURE_2D, 0, source_x, source_y, painted->width,
                        painted->height, GL_RGBA, GL_UNSIGNED_BYTE,
                        scratch_.data());
        ++result.metrics.texture_uploads;
        result.metrics.texture_upload_bytes += row_bytes * painted->height;
      }
      const auto local = *surface_local_rectangle(surface);
      // Preserve the software reference path's historical clipped-source
      // placement, including its source-origin contribution.
      const float left = 2.0F * (surface.logical_x + 2 * local.x) /
                             output.logical_width - 1.0F;
      const float right = 2.0F *
                              (surface.logical_x + 2 * local.x + local.width) /
                              output.logical_width - 1.0F;
      const float top = 1.0F - 2.0F * (surface.logical_y + 2 * local.y) /
                                   output.logical_height;
      const float bottom = 1.0F - 2.0F *
          (surface.logical_y + 2 * local.y + local.height) /
          output.logical_height;
      const float u0 = static_cast<float>(local.x) / mapping->width();
      const float u1 = static_cast<float>(local.x + local.width) /
                       mapping->width();
      const float v0 = static_cast<float>(local.y) / mapping->height();
      const float v1 = static_cast<float>(local.y + local.height) /
                       mapping->height();
      const std::array<float, 8> positions{left, top, left, bottom, right, top,
                                           right, bottom};
      const std::array<float, 8> texcoords{u0, v0, u0, v1, u1, v0, u1, v1};
      glBindTexture(GL_TEXTURE_2D, textures_.at(buffer_id).name);
      glUniform1f(opacity_, static_cast<float>(surface.opacity) /
                                static_cast<float>(GWIPC_OPACITY_ONE));
      glUniform1f(xrgb_, mapping->pixel_format() == GWIPC_PIXEL_FORMAT_XRGB8888
                              ? 1.0F : 0.0F);
      glEnableVertexAttribArray(position_);
      glEnableVertexAttribArray(texcoord_);
      glVertexAttribPointer(position_, 2, GL_FLOAT, GL_FALSE, 0,
                            positions.data());
      glVertexAttribPointer(texcoord_, 2, GL_FLOAT, GL_FALSE, 0,
                            texcoords.data());
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
  }
  glFinish();
}

void GlesSceneRenderer::readback_damage(const RenderFrameRequest& request,
                                        RenderFrameResult& result) {
  const auto& output = *request.scene.output;
  for (const auto& damage : request.damage) {
    const std::size_t bytes = static_cast<std::size_t>(damage.width) *
                              damage.height * 4U;
    scratch_.resize(bytes);
    glReadPixels(damage.x, output.logical_height - damage.y - damage.height,
                 damage.width, damage.height, GL_RGBA, GL_UNSIGNED_BYTE,
                 scratch_.data());
    result.metrics.readback_bytes += bytes;
    for (std::uint32_t row = 0; row < damage.height; ++row) {
      const auto source_row = damage.height - 1U - row;
      for (std::uint32_t column = 0; column < damage.width; ++column) {
        const auto source = (static_cast<std::size_t>(source_row) * damage.width +
                             column) * 4U;
        result.frame.pixels()[static_cast<std::size_t>(damage.y + row) *
                                  output.logical_width + damage.x + column] =
            0xff000000U | (static_cast<std::uint32_t>(scratch_[source]) << 16U) |
            (static_cast<std::uint32_t>(scratch_[source + 1U]) << 8U) |
            scratch_[source + 2U];
      }
    }
  }
}

} // namespace gw::render::gles
