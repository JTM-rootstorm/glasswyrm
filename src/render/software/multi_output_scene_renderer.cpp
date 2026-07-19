#include "render/software/multi_output_scene_renderer.hpp"

#include "compositor/scene_validation.hpp"
#include "output/model/mapping.hpp"
#include "render/software/blend.hpp"
#include "render/software/pixel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>

namespace gw::render::software {
namespace {

using glasswyrm::output::LogicalRectangle;
using glasswyrm::output::LogicalSamplePoint;
using glasswyrm::output::OutputFrameResult;
using glasswyrm::output::OutputMapping;
using glasswyrm::output::OutputTransform;
using glasswyrm::output::PhysicalPoint;
using glasswyrm::output::RationalScale;

struct SourceImage {
  std::span<const std::byte> bytes;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t stride{};
  PixelFormat format{};
};

struct FractionFloor {
  std::int64_t whole{};
  std::uint64_t remainder{};
};

[[nodiscard]] FractionFloor floor_fraction(const std::int64_t numerator,
                                           const std::uint64_t denominator) {
  auto whole = numerator / static_cast<std::int64_t>(denominator);
  auto remainder = numerator % static_cast<std::int64_t>(denominator);
  if (remainder < 0) {
    --whole;
    remainder += static_cast<std::int64_t>(denominator);
  }
  return {whole, static_cast<std::uint64_t>(remainder)};
}

[[nodiscard]] std::optional<compositor::Rectangle>
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

[[nodiscard]] bool member_of_output(
    const compositor::SurfaceOutputMembership &membership,
    const std::uint64_t output_id) noexcept {
  return std::ranges::find(membership.output_ids, output_id) !=
         membership.output_ids.end();
}

[[nodiscard]] bool inside(const LogicalSamplePoint point,
                          const compositor::Rectangle rectangle) noexcept {
  const auto denominator = static_cast<std::int64_t>(point.denominator);
  const auto left = static_cast<std::int64_t>(rectangle.x) * denominator;
  const auto top = static_cast<std::int64_t>(rectangle.y) * denominator;
  const auto right =
      (static_cast<std::int64_t>(rectangle.x) + rectangle.width) * denominator;
  const auto bottom =
      (static_cast<std::int64_t>(rectangle.y) + rectangle.height) * denominator;
  return point.x_numerator >= left && point.x_numerator < right &&
         point.y_numerator >= top && point.y_numerator < bottom;
}

[[nodiscard]] bool source_image(const compositor::BufferMapping &mapping,
                                const gwipc_surface_upsert &surface,
                                SourceImage &image, std::string &error) {
  const auto client_scale = surface.scale_numerator;
  const auto expected_width =
      static_cast<std::uint64_t>(surface.logical_width) * client_scale;
  const auto expected_height =
      static_cast<std::uint64_t>(surface.logical_height) * client_scale;
  if (mapping.width() != expected_width || mapping.height() != expected_height) {
    error = "surface buffer dimensions do not match its client scale";
    return false;
  }
  const auto row_bytes = static_cast<std::uint64_t>(mapping.width()) * 4U;
  const auto required = static_cast<std::uint64_t>(mapping.height() - 1U) *
                            mapping.stride() +
                        row_bytes;
  if (mapping.stride() < row_bytes || required > mapping.bytes().size()) {
    error = "surface buffer view is invalid";
    return false;
  }
  image = {mapping.bytes(), mapping.width(), mapping.height(), mapping.stride(),
           mapping.pixel_format() == GWIPC_PIXEL_FORMAT_XRGB8888
               ? PixelFormat::Xrgb8888
               : PixelFormat::Argb8888Premultiplied};
  if (image.format == PixelFormat::Argb8888Premultiplied) {
    for (std::uint32_t y = 0; y < image.height; ++y) {
      for (std::uint32_t x = 0; x < image.width; ++x) {
        const auto offset = static_cast<std::size_t>(y) * image.stride +
                            static_cast<std::size_t>(x) * 4U;
        if (!is_premultiplied(
                unpack_argb8888(load_u32(image.bytes.data() + offset)))) {
          error = "ARGB buffer contains a non-premultiplied pixel";
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] Pixel source_pixel(const SourceImage &image, std::int64_t x,
                                 std::int64_t y) noexcept {
  x = std::clamp<std::int64_t>(x, 0, image.width - 1U);
  y = std::clamp<std::int64_t>(y, 0, image.height - 1U);
  const auto offset = static_cast<std::size_t>(y) * image.stride +
                      static_cast<std::size_t>(x) * 4U;
  const auto word = load_u32(image.bytes.data() + offset);
  return image.format == PixelFormat::Xrgb8888 ? unpack_xrgb8888(word)
                                                : unpack_argb8888(word);
}

[[nodiscard]] std::uint8_t interpolate_channel(
    const std::array<std::uint8_t, 4> channels, const std::uint64_t x_weight,
    const std::uint64_t y_weight, const std::uint64_t denominator) noexcept {
  const auto inverse_x = denominator - x_weight;
  const auto inverse_y = denominator - y_weight;
  const auto divisor = denominator * denominator;
  const auto weighted =
      static_cast<std::uint64_t>(channels[0]) * inverse_x * inverse_y +
      static_cast<std::uint64_t>(channels[1]) * x_weight * inverse_y +
      static_cast<std::uint64_t>(channels[2]) * inverse_x * y_weight +
      static_cast<std::uint64_t>(channels[3]) * x_weight * y_weight;
  return static_cast<std::uint8_t>((weighted + divisor / 2U) / divisor);
}

[[nodiscard]] Pixel sample(const SourceImage &image,
                           const gwipc_surface_upsert &surface,
                           const LogicalSamplePoint point,
                           const SamplingFilter filter) noexcept {
  const auto denominator = static_cast<std::int64_t>(point.denominator);
  const auto local_x = point.x_numerator -
                       static_cast<std::int64_t>(surface.logical_x) *
                           denominator;
  const auto local_y = point.y_numerator -
                       static_cast<std::int64_t>(surface.logical_y) *
                           denominator;
  const auto client_scale = static_cast<std::int64_t>(surface.scale_numerator);
  if (filter != SamplingFilter::Bilinear) {
    return source_pixel(image, local_x * client_scale / denominator,
                        local_y * client_scale / denominator);
  }

  const auto fixed_denominator =
      static_cast<std::uint64_t>(point.denominator) * 2U;
  const auto x = floor_fraction(local_x * client_scale * 2 - denominator,
                                fixed_denominator);
  const auto y = floor_fraction(local_y * client_scale * 2 - denominator,
                                fixed_denominator);
  const std::array<Pixel, 4> pixels{{
      source_pixel(image, x.whole, y.whole),
      source_pixel(image, x.whole + 1, y.whole),
      source_pixel(image, x.whole, y.whole + 1),
      source_pixel(image, x.whole + 1, y.whole + 1),
  }};
  return Pixel{
      interpolate_channel({pixels[0].red, pixels[1].red, pixels[2].red,
                           pixels[3].red},
                          x.remainder, y.remainder, fixed_denominator),
      interpolate_channel({pixels[0].green, pixels[1].green, pixels[2].green,
                           pixels[3].green},
                          x.remainder, y.remainder, fixed_denominator),
      interpolate_channel({pixels[0].blue, pixels[1].blue, pixels[2].blue,
                           pixels[3].blue},
                          x.remainder, y.remainder, fixed_denominator),
      static_cast<std::uint8_t>(
          image.format == PixelFormat::Xrgb8888
              ? 255
              : interpolate_channel(
                    {pixels[0].alpha, pixels[1].alpha, pixels[2].alpha,
                     pixels[3].alpha},
                    x.remainder, y.remainder, fixed_denominator))};
}

[[nodiscard]] OutputMapping mapping(const gwipc_output_upsert &output) {
  return {{output.logical_x, output.logical_y},
          {output.logical_width, output.logical_height},
          {output.physical_pixel_width, output.physical_pixel_height},
          {output.scale_numerator, output.scale_denominator},
          static_cast<OutputTransform>(output.transform)};
}

[[nodiscard]] bool compatible_previous(
    const glasswyrm::output::SoftwareFrameSet *previous,
    const gwipc_output_upsert &output,
    const glasswyrm::output::OutputFrameResult *&frame) noexcept {
  frame = nullptr;
  if (!previous || !previous->finalized())
    return false;
  const auto found = previous->outputs().find(output.output_id);
  if (found == previous->outputs().end())
    return false;
  const auto &candidate = found->second;
  if (candidate.output.width != output.physical_pixel_width ||
      candidate.output.height != output.physical_pixel_height ||
      candidate.scale !=
          RationalScale{output.scale_numerator, output.scale_denominator} ||
      candidate.transform != static_cast<OutputTransform>(output.transform))
    return false;
  frame = &candidate;
  return true;
}

void record_filter(OutputSoftwareRenderMetrics &metrics,
                   const SamplingFilter filter) noexcept {
  metrics.used_direct |= filter == SamplingFilter::Direct;
  metrics.used_nearest |= filter == SamplingFilter::Nearest;
  metrics.used_bilinear |= filter == SamplingFilter::Bilinear;
}

[[nodiscard]] bool render_surface(
    OutputFrameResult &output_frame, const OutputMapping &output_mapping,
    const compositor::Rectangle damage, const gwipc_surface_upsert &surface,
    const compositor::SurfaceOutputMembership &membership,
    const BufferMappingMap &mappings, const SurfaceAttachmentMap &attachments,
    std::map<std::uint64_t, SourceImage> &source_cache,
    OutputSoftwareRenderMetrics &metrics, std::string &error) {
  if (!surface.visible || surface.opacity == 0 ||
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY ||
      !member_of_output(membership, output_frame.output.output_id))
    return true;
  const auto logical_bounds = surface_rectangle(surface);
  if (!logical_bounds)
    return true;
  const auto physical_bounds =
      glasswyrm::output::map_logical_rectangle_to_native(
          output_mapping,
          LogicalRectangle{logical_bounds->x, logical_bounds->y,
                           logical_bounds->width, logical_bounds->height});
  if (!physical_bounds)
    return true;
  const auto painted = compositor::intersection(
      damage, {static_cast<std::int32_t>(physical_bounds->x),
               static_cast<std::int32_t>(physical_bounds->y),
               physical_bounds->width, physical_bounds->height});
  if (!painted)
    return true;

  auto cached = source_cache.find(surface.surface_id);
  if (cached == source_cache.end()) {
    const auto attachment = attachments.find(surface.surface_id);
    if (attachment == attachments.end()) {
      error = "visible surface has no renderer attachment";
      return false;
    }
    const auto found_mapping = mappings.find(attachment->second);
    if (found_mapping == mappings.end() || !found_mapping->second) {
      error = "renderer attachment has no buffer mapping";
      return false;
    }
    SourceImage image;
    if (!source_image(*found_mapping->second, surface, image, error))
      return false;
    cached = source_cache.emplace(surface.surface_id, image).first;
  }
  const auto &image = cached->second;
  const auto filter = select_sampling_filter(output_mapping.scale,
                                             surface.scale_numerator);
  record_filter(metrics, filter);
  auto pixels = output_frame.frame.pixels();
  for (std::uint32_t y = 0; y < painted->height; ++y) {
    for (std::uint32_t x = 0; x < painted->width; ++x) {
      const auto native_x = static_cast<std::uint32_t>(painted->x) + x;
      const auto native_y = static_cast<std::uint32_t>(painted->y) + y;
      const auto logical =
          glasswyrm::output::map_native_pixel_center_to_logical(
              output_mapping, PhysicalPoint{native_x, native_y});
      if (!logical || !inside(*logical, *logical_bounds))
        continue;
      const auto source = sample(image, surface, *logical, filter);
      const auto index = static_cast<std::size_t>(native_y) *
                             output_frame.output.width +
                         native_x;
      const auto destination = unpack_xrgb8888(pixels[index]);
      pixels[index] = pack_xrgb8888(blend(source, destination, surface.opacity));
      ++metrics.sampled_pixels;
    }
  }
  return true;
}

} // namespace

SamplingFilter select_sampling_filter(const RationalScale output_scale,
                                      const std::uint32_t client_buffer_scale)
    noexcept {
  if (client_buffer_scale == 0 || output_scale.numerator == 0 ||
      output_scale.denominator == 0)
    return SamplingFilter::Bilinear;
  const auto client_denominator =
      static_cast<std::uint64_t>(output_scale.denominator) *
      client_buffer_scale;
  if (output_scale.numerator == client_denominator)
    return SamplingFilter::Direct;
  if (output_scale.numerator > client_denominator &&
      output_scale.numerator % client_denominator == 0)
    return SamplingFilter::Nearest;
  return SamplingFilter::Bilinear;
}

SoftwareFrameSetRenderResult MultiOutputSoftwareSceneRenderer::render(
    const SoftwareFrameSetRenderRequest &request) const {
  SoftwareFrameSetRenderResult result;
  if (request.scene_model.profile() != compositor::SceneProfile::OutputModel ||
      !request.scene_model.initial_snapshot_received() ||
      request.commit_id == 0 || request.generation == 0 ||
      request.ordinal == 0 ||
      compositor::validate_output_model_scene(
          request.scene_model.committed()) != GWIPC_FRAME_ACCEPTED) {
    result.error = "renderer requires a committed output-model scene";
    return result;
  }
  if (request.previous && !request.previous->finalized()) {
    result.error = "previous software frame set is not finalized";
    return result;
  }
  const auto &scene = request.scene_model.committed();
  glasswyrm::output::SoftwareFrameSet staged_frames;
  std::map<std::uint64_t, SourceImage> source_cache;
  for (const auto &[output_id, unused] : request.damage) {
    (void)unused;
    const auto output = scene.outputs.find(output_id);
    if (output == scene.outputs.end() || !output->second.enabled) {
      result.error = "physical damage names an unavailable output";
      return result;
    }
  }

  const auto stacking_order = request.scene_model.stacking_order();
  for (const auto &[output_id, output] : scene.outputs) {
    if (!output.enabled)
      continue;
    const auto output_mapping = mapping(output);
    if (!glasswyrm::output::valid_output_mapping(output_mapping)) {
      result.error = "output has invalid software mapping metadata";
      return result;
    }
    OutputFrameResult rendered;
    rendered.output = {output_id, output.physical_pixel_width,
                       output.physical_pixel_height,
                       output.refresh_millihertz};
    rendered.logical = {output.logical_x, output.logical_y,
                        output.logical_width, output.logical_height};
    rendered.scale = {output.scale_numerator, output.scale_denominator};
    rendered.transform = static_cast<OutputTransform>(output.transform);
    if (!rendered.frame.configure(output_id, output.physical_pixel_width,
                                  output.physical_pixel_height, result.error))
      return result;

    const OutputFrameResult *previous = nullptr;
    const bool copied = compatible_previous(request.previous, output, previous);
    if (copied)
      std::ranges::copy(previous->frame.pixels(), rendered.frame.pixels().begin());
    const auto supplied_damage = request.damage.find(output_id);
    if (!copied) {
      rendered.damage.push_back(
          {0, 0, output.physical_pixel_width, output.physical_pixel_height});
    } else if (supplied_damage != request.damage.end()) {
      rendered.damage = supplied_damage->second;
    }
    if (rendered.damage.size() > GWIPC_MAXIMUM_DAMAGE_RECTANGLES) {
      result.error = "software output frame exceeds the damage limit";
      return result;
    }

    auto &metrics = result.metrics[output_id];
    metrics.damage_rectangles = rendered.damage.size();
    for (const auto rectangle : rendered.damage) {
      if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width == 0 ||
          rectangle.height == 0 ||
          static_cast<std::uint64_t>(rectangle.x) + rectangle.width >
              output.physical_pixel_width ||
          static_cast<std::uint64_t>(rectangle.y) + rectangle.height >
              output.physical_pixel_height) {
        result.error = "physical damage is outside its output";
        return result;
      }
      auto pixels = rendered.frame.pixels();
      for (std::uint32_t y = 0; y < rectangle.height; ++y)
        std::fill_n(pixels.begin() +
                        static_cast<std::size_t>(rectangle.y + y) *
                            output.physical_pixel_width +
                        rectangle.x,
                    rectangle.width,
                    glasswyrm::output::SoftwareFrame::kClearPixel);
      for (const auto surface_id : stacking_order) {
        const auto &surface = scene.surfaces.at(surface_id);
        const auto membership = scene.surface_outputs.find(surface_id);
        if (membership == scene.surface_outputs.end())
          continue;
        if (!render_surface(rendered, output_mapping, rectangle, surface,
                            membership->second, request.mappings,
                            request.attachments, source_cache, metrics,
                            result.error)) {
          result.disposition = RenderDisposition::InvalidBuffer;
          return result;
        }
      }
    }
    if (!staged_frames.append(std::move(rendered), result.error))
      return result;
  }
  if (!staged_frames.finalize(scene.configuration_generation,
                              scene.primary_output_id, request.commit_id,
                              request.generation, request.ordinal,
                              result.error))
    return result;
  result.frames = std::move(staged_frames);
  result.disposition = RenderDisposition::Complete;
  result.error.clear();
  return result;
}

} // namespace gw::render::software
