#include "glasswyrmd/picture.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace glasswyrm::server {
namespace {

bool is_premultiplied(const PremultipliedColor color) noexcept {
  return color.red <= color.alpha && color.green <= color.alpha &&
         color.blue <= color.alpha;
}

bool valid_rectangle(const geometry::Rectangle rectangle,
                     const std::int32_t origin_x,
                     const std::int32_t origin_y) noexcept {
  const auto left = static_cast<std::int64_t>(origin_x) + rectangle.x;
  const auto top = static_cast<std::int64_t>(origin_y) + rectangle.y;
  const auto right = left + rectangle.width;
  const auto bottom = top + rectangle.height;
  return left >= std::numeric_limits<std::int32_t>::min() &&
         top >= std::numeric_limits<std::int32_t>::min() &&
         right <= std::numeric_limits<std::int32_t>::max() &&
         bottom <= std::numeric_limits<std::int32_t>::max();
}

}  // namespace

const PictureFormatDescriptor* find_picture_format(
    const PictureFormatId id) noexcept {
  const auto found = std::ranges::find(kCanonicalPictureFormats, id,
                                       &PictureFormatDescriptor::id);
  return found == kCanonicalPictureFormats.end() ? nullptr : &*found;
}

std::optional<Picture> Picture::create_drawable(
    const std::uint32_t drawable, const PictureFormatId format,
    const std::uint8_t depth, const std::uint8_t bits_per_pixel) noexcept {
  const auto* descriptor = find_picture_format(format);
  if (drawable == 0 || descriptor == nullptr || descriptor->depth != depth ||
      descriptor->bits_per_pixel != bits_per_pixel)
    return std::nullopt;
  return Picture(format, DrawablePictureSource{drawable});
}

std::optional<Picture> Picture::create_solid(
    const PremultipliedColor color) noexcept {
  if (!is_premultiplied(color)) return std::nullopt;
  return Picture(PictureFormatId::Argb32, SolidPictureSource{color});
}

PictureStatus Picture::apply_attributes(
    const PictureAttributeUpdate& update) noexcept {
  if (update.unsupported_mask != 0)
    return PictureStatus::UnsupportedAttribute;
  if ((update.repeat && *update.repeat != 0) ||
      (update.alpha_map && *update.alpha_map != 0) ||
      (update.component_alpha && *update.component_alpha != 0) ||
      (update.subwindow_mode && *update.subwindow_mode != 0))
    return PictureStatus::BadAttributeValue;

  const auto next_x = update.clip_x_origin.value_or(attributes_.clip_x_origin);
  const auto next_y = update.clip_y_origin.value_or(attributes_.clip_y_origin);
  if (attributes_.clip_rectangles &&
      !std::ranges::all_of(*attributes_.clip_rectangles,
                           [=](const auto rectangle) {
                             return valid_rectangle(rectangle, next_x, next_y);
                           }))
    return PictureStatus::BadAttributeValue;

  if (update.clip_x_origin)
    attributes_.clip_x_origin = *update.clip_x_origin;
  if (update.clip_y_origin)
    attributes_.clip_y_origin = *update.clip_y_origin;
  return PictureStatus::Success;
}

PictureStatus Picture::set_clip_rectangles(
    const std::int32_t origin_x, const std::int32_t origin_y,
    const std::span<const geometry::Rectangle> rectangles) noexcept {
  if (rectangles.size() > kMaximumClipRectangles)
    return PictureStatus::TooManyClipRectangles;
  if (!std::ranges::all_of(rectangles, [=](const auto rectangle) {
        return valid_rectangle(rectangle, origin_x, origin_y);
      }))
    return PictureStatus::InvalidClipRectangle;
  try {
    std::vector<geometry::Rectangle> next(rectangles.begin(), rectangles.end());
    attributes_.clip_rectangles = std::move(next);
    attributes_.clip_x_origin = origin_x;
    attributes_.clip_y_origin = origin_y;
    return PictureStatus::Success;
  } catch (const std::bad_alloc&) {
    return PictureStatus::BadAlloc;
  }
}

}  // namespace glasswyrm::server
