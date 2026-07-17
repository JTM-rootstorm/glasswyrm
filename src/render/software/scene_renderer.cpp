#include "render/software/scene_renderer.hpp"

#include "render/software/renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace gw::render::software {
namespace {

std::span<std::byte> pixel_bytes(std::span<std::uint32_t> pixels) {
  return {reinterpret_cast<std::byte*>(pixels.data()), pixels.size_bytes()};
}

std::optional<compositor::Rectangle>
surface_bounds(const gwipc_surface_upsert& surface,
               const gwipc_output_upsert& output) {
  compositor::Rectangle local{0, 0, surface.logical_width,
                              surface.logical_height};
  if (surface.clipping) {
    const auto clipped = compositor::intersection(
        local, {surface.clip_x, surface.clip_y, surface.clip_width,
                surface.clip_height});
    if (!clipped) return std::nullopt;
    local = *clipped;
  }
  const auto placed =
      compositor::translate(local, surface.logical_x, surface.logical_y);
  if (!placed) return std::nullopt;
  return compositor::intersection(
      *placed, {0, 0, output.logical_width, output.logical_height});
}

RenderFrameResult invalid_buffer(std::string error,
                                 const RendererMetrics metrics) {
  RenderFrameResult result;
  result.disposition = RenderDisposition::InvalidBuffer;
  result.metrics = metrics;
  result.error = std::move(error);
  return result;
}

} // namespace

RenderFrameResult
SoftwareSceneRenderer::render(const RenderFrameRequest& request) {
  RenderFrameResult result;
  result.metrics.damage_rectangles = request.damage.size();
  if (!request.scene.output || !request.scene.output->enabled) {
    result.error = "renderer requires one enabled output";
    return result;
  }
  const auto& output = *request.scene.output;
  if (!result.frame.configure(output.output_id, output.logical_width,
                              output.logical_height, result.error))
    return result;
  if (request.previous && request.previous->enabled() &&
      request.previous->id() == result.frame.id() &&
      request.previous->width() == result.frame.width() &&
      request.previous->height() == result.frame.height()) {
    std::copy(request.previous->pixels().begin(),
              request.previous->pixels().end(), result.frame.pixels().begin());
  }

  FramebufferView framebuffer{pixel_bytes(result.frame.pixels()),
                              result.frame.width(), result.frame.height(),
                              result.frame.width() * 4U};
  for (const auto& rectangle : request.damage) {
    if (clear(framebuffer, rectangle) != RenderResult::Success)
      return invalid_buffer("framebuffer damage is invalid", result.metrics);
    for (const auto surface_id : request.stacking_order) {
      const auto surface_it = request.scene.surfaces.find(surface_id);
      if (surface_it == request.scene.surfaces.end())
        return invalid_buffer("renderer stacking order is invalid",
                              result.metrics);
      const auto& surface = surface_it->second;
      if (!surface.visible || surface.opacity == 0 ||
          surface.presentation_flags ==
              GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
        continue;
      const auto bounds = surface_bounds(surface, output);
      if (!bounds || !compositor::intersection(*bounds, rectangle)) continue;
      const auto attachment = request.attachments.find(surface_id);
      if (attachment == request.attachments.end())
        return invalid_buffer("visible surface has no renderer attachment",
                              result.metrics);
      const auto mapping = request.mappings.find(attachment->second);
      if (mapping == request.mappings.end() || !mapping->second)
        return invalid_buffer("renderer attachment has no buffer mapping",
                              result.metrics);
      compositor::Rectangle local{0, 0, surface.logical_width,
                                  surface.logical_height};
      if (surface.clipping)
        local = {surface.clip_x, surface.clip_y, surface.clip_width,
                 surface.clip_height};
      const ImageView image{
          mapping->second->bytes(), mapping->second->width(),
          mapping->second->height(), mapping->second->stride(),
          mapping->second->pixel_format() == GWIPC_PIXEL_FORMAT_XRGB8888
              ? PixelFormat::Xrgb8888
              : PixelFormat::Argb8888Premultiplied};
      const auto rendered = composite(
          framebuffer, image, local, surface.logical_x + local.x,
          surface.logical_y + local.y, surface.opacity);
      if (rendered != RenderResult::Success) {
        return invalid_buffer(
            rendered == RenderResult::InvalidPremultipliedPixel
                ? "ARGB buffer contains a non-premultiplied pixel"
                : "buffer view is invalid",
            result.metrics);
      }
    }
  }
  result.disposition = RenderDisposition::Complete;
  result.error.clear();
  return result;
}

} // namespace gw::render::software
