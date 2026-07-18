#include "glasswyrmd/xfixes_region.hpp"

#include <algorithm>
#include <limits>
#include <tuple>

namespace glasswyrm::server {
namespace {

using geometry::Rectangle;

bool valid(const Rectangle rectangle) noexcept {
  if (rectangle.width == 0 || rectangle.height == 0) return false;
  const auto right = static_cast<std::int64_t>(rectangle.x) + rectangle.width;
  const auto bottom = static_cast<std::int64_t>(rectangle.y) + rectangle.height;
  return right <= std::numeric_limits<std::int32_t>::max() &&
         bottom <= std::numeric_limits<std::int32_t>::max();
}

std::vector<Rectangle> subtract_one(const Rectangle source,
                                    const Rectangle removed) {
  const auto overlap = geometry::intersect(source, removed);
  if (!overlap) return {source};
  const auto source_right = static_cast<std::int64_t>(source.x) + source.width;
  const auto source_bottom = static_cast<std::int64_t>(source.y) + source.height;
  const auto overlap_right = static_cast<std::int64_t>(overlap->x) + overlap->width;
  const auto overlap_bottom = static_cast<std::int64_t>(overlap->y) + overlap->height;
  std::vector<Rectangle> result;
  if (source.y < overlap->y)
    result.push_back({source.x, source.y, source.width,
                      static_cast<std::uint32_t>(overlap->y - source.y)});
  if (overlap_bottom < source_bottom)
    result.push_back({source.x, static_cast<std::int32_t>(overlap_bottom),
                      source.width,
                      static_cast<std::uint32_t>(source_bottom - overlap_bottom)});
  if (source.x < overlap->x)
    result.push_back({source.x, overlap->y,
                      static_cast<std::uint32_t>(overlap->x - source.x),
                      overlap->height});
  if (overlap_right < source_right)
    result.push_back({static_cast<std::int32_t>(overlap_right), overlap->y,
                      static_cast<std::uint32_t>(source_right - overlap_right),
                      overlap->height});
  return result;
}

bool merge_pair(Rectangle& left, const Rectangle right) noexcept {
  if (left.y == right.y && left.height == right.height &&
      static_cast<std::int64_t>(left.x) + left.width == right.x) {
    left.width += right.width;
    return true;
  }
  if (left.x == right.x && left.width == right.width &&
      static_cast<std::int64_t>(left.y) + left.height == right.y) {
    left.height += right.height;
    return true;
  }
  return false;
}

void sort_rectangles(std::vector<Rectangle>& rectangles) {
  std::ranges::sort(rectangles, [](const auto& left, const auto& right) {
    return std::tie(left.y, left.x, left.height, left.width) <
           std::tie(right.y, right.x, right.height, right.width);
  });
}

bool merge_adjacent(std::vector<Rectangle>& rectangles) {
  bool any = false;
  for (std::size_t left = 0; left < rectangles.size(); ++left) {
    for (std::size_t right = left + 1; right < rectangles.size(); ++right) {
      if (!merge_pair(rectangles[left], rectangles[right])) continue;
      rectangles.erase(rectangles.begin() + static_cast<std::ptrdiff_t>(right));
      any = true;
      --left;
      break;
    }
  }
  return any;
}

bool add_rectangle(std::vector<Rectangle>& destination,
                   const Rectangle rectangle) {
  if (!valid(rectangle)) return rectangle.width == 0 || rectangle.height == 0;
  std::vector<Rectangle> fragments{rectangle};
  for (const auto existing : destination) {
    std::vector<Rectangle> next;
    for (const auto fragment : fragments) {
      auto pieces = subtract_one(fragment, existing);
      next.insert(next.end(), pieces.begin(), pieces.end());
      if (next.size() + destination.size() > kMaximumXFixesRegionRectangles)
        return false;
    }
    fragments = std::move(next);
    if (fragments.empty()) break;
  }
  destination.insert(destination.end(), fragments.begin(), fragments.end());
  while (merge_adjacent(destination)) {}
  sort_rectangles(destination);
  return destination.size() <= kMaximumXFixesRegionRectangles;
}

}  // namespace

std::optional<std::vector<geometry::Rectangle>> normalize_region(
    const std::span<const geometry::Rectangle> rectangles) {
  if (rectangles.size() > kMaximumXFixesRegionRectangles) return std::nullopt;
  std::vector<Rectangle> result;
  for (const auto rectangle : rectangles)
    if (!add_rectangle(result, rectangle)) return std::nullopt;
  return result;
}

std::optional<std::vector<geometry::Rectangle>> union_regions(
    const std::span<const geometry::Rectangle> left,
    const std::span<const geometry::Rectangle> right) {
  auto result = normalize_region(left);
  if (!result) return std::nullopt;
  for (const auto rectangle : right)
    if (!add_rectangle(*result, rectangle)) return std::nullopt;
  return result;
}

std::optional<std::vector<geometry::Rectangle>> intersect_regions(
    const std::span<const geometry::Rectangle> left,
    const std::span<const geometry::Rectangle> right) {
  std::vector<Rectangle> intersections;
  for (const auto first : left)
    for (const auto second : right)
      if (const auto overlap = geometry::intersect(first, second)) {
        if (!add_rectangle(intersections, *overlap)) return std::nullopt;
      }
  return intersections;
}

std::optional<std::vector<geometry::Rectangle>> subtract_regions(
    const std::span<const geometry::Rectangle> left,
    const std::span<const geometry::Rectangle> right) {
  auto result = normalize_region(left);
  if (!result) return std::nullopt;
  for (const auto removed : right) {
    std::vector<Rectangle> next;
    for (const auto source : *result) {
      auto fragments = subtract_one(source, removed);
      next.insert(next.end(), fragments.begin(), fragments.end());
      if (next.size() > kMaximumXFixesRegionRectangles) return std::nullopt;
    }
    result = normalize_region(next);
    if (!result) return std::nullopt;
  }
  return result;
}

std::optional<std::vector<geometry::Rectangle>> translate_region(
    const std::span<const geometry::Rectangle> source, const std::int16_t dx,
    const std::int16_t dy) {
  std::vector<Rectangle> result;
  result.reserve(source.size());
  for (const auto rectangle : source) {
    const auto x = static_cast<std::int64_t>(rectangle.x) + dx;
    const auto y = static_cast<std::int64_t>(rectangle.y) + dy;
    if (x < std::numeric_limits<std::int16_t>::min() ||
        x > std::numeric_limits<std::int16_t>::max() ||
        y < std::numeric_limits<std::int16_t>::min() ||
        y > std::numeric_limits<std::int16_t>::max())
      return std::nullopt;
    result.push_back({static_cast<std::int32_t>(x),
                      static_cast<std::int32_t>(y), rectangle.width,
                      rectangle.height});
  }
  return result;
}

geometry::Rectangle region_extents(
    const std::span<const geometry::Rectangle> rectangles) noexcept {
  if (rectangles.empty()) return {};
  std::int64_t left = rectangles.front().x;
  std::int64_t top = rectangles.front().y;
  std::int64_t right = left + rectangles.front().width;
  std::int64_t bottom = top + rectangles.front().height;
  for (const auto rectangle : rectangles) {
    left = std::min(left, static_cast<std::int64_t>(rectangle.x));
    top = std::min(top, static_cast<std::int64_t>(rectangle.y));
    right = std::max(right,
                     static_cast<std::int64_t>(rectangle.x) + rectangle.width);
    bottom = std::max(bottom,
                      static_cast<std::int64_t>(rectangle.y) + rectangle.height);
  }
  return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
          static_cast<std::uint32_t>(right - left),
          static_cast<std::uint32_t>(bottom - top)};
}

}  // namespace glasswyrm::server
