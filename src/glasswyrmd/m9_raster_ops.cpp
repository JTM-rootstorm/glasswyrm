#include "glasswyrmd/m9_raster_ops.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace glasswyrm::server {
namespace {
bool line_may_touch(const PixelStorage& destination, const RasterPoint first,
                    const RasterPoint second) noexcept {
  const auto right = static_cast<std::int32_t>(destination.width() - 1U);
  const auto bottom = static_cast<std::int32_t>(destination.height() - 1U);
  return !((first.x < 0 && second.x < 0) ||
           (first.y < 0 && second.y < 0) ||
           (first.x > right && second.x > right) ||
           (first.y > bottom && second.y > bottom));
}

void plot(PixelStorage& destination, const std::int32_t x,
          const std::int32_t y, const std::uint32_t foreground,
          const std::uint32_t plane_mask) noexcept {
  if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= destination.width() ||
      static_cast<std::uint32_t>(y) >= destination.height())
    return;
  const auto mask = plane_mask & 0x00ffffffU;
  auto& pixel = destination.at(static_cast<std::uint32_t>(x),
                               static_cast<std::uint32_t>(y));
  pixel = 0xff000000U | (foreground & mask) |
          (pixel & ~mask & 0x00ffffffU);
}
}  // namespace

std::uint64_t line_raster_work(const PixelStorage& destination,
                               const RasterPoint first,
                               const RasterPoint second) noexcept {
  if (!line_may_touch(destination, first, second)) return 0;
  const auto dx = std::abs(static_cast<std::int64_t>(second.x) - first.x);
  const auto dy = std::abs(static_cast<std::int64_t>(second.y) - first.y);
  return static_cast<std::uint64_t>(std::max(dx, dy)) + 1U;
}

std::uint64_t ellipse_raster_work(const PixelStorage& destination,
                                  const RasterEllipse ellipse) noexcept {
  if (ellipse.width == 0 || ellipse.height == 0) return 0;
  const auto clipped = geometry::intersect(
      {ellipse.x, ellipse.y, ellipse.width, ellipse.height},
      {0, 0, destination.width(), destination.height()});
  return clipped ? static_cast<std::uint64_t>(clipped->width) * clipped->height
                 : 0;
}

void draw_line(PixelStorage& destination, RasterPoint first,
               const RasterPoint second, const std::uint32_t foreground,
               const std::uint32_t plane_mask) noexcept {
  if (!line_may_touch(destination, first, second)) return;
  auto dx = std::abs(static_cast<std::int64_t>(second.x) - first.x);
  const auto sx = first.x < second.x ? 1 : -1;
  auto dy = -std::abs(static_cast<std::int64_t>(second.y) - first.y);
  const auto sy = first.y < second.y ? 1 : -1;
  auto error = dx + dy;
  for (;;) {
    plot(destination, first.x, first.y, foreground, plane_mask);
    if (first.x == second.x && first.y == second.y) break;
    const auto doubled = error * 2;
    if (doubled >= dy) {
      error += dy;
      first.x += sx;
    }
    if (doubled <= dx) {
      error += dx;
      first.y += sy;
    }
  }
}

void draw_segments(PixelStorage& destination,
                   const std::span<const RasterSegment> segments,
                   const std::uint32_t foreground,
                   const std::uint32_t plane_mask) noexcept {
  for (const auto segment : segments)
    draw_line(destination, segment.first, segment.second, foreground,
              plane_mask);
}

void fill_convex_polygon(PixelStorage& destination,
                         const std::span<const RasterPoint> points,
                         const std::uint32_t foreground,
                         const std::uint32_t plane_mask) noexcept {
  if (points.size() < 3) return;
  auto minimum_y = points.front().y;
  auto maximum_y = points.front().y;
  for (const auto point : points) {
    minimum_y = std::min(minimum_y, point.y);
    maximum_y = std::max(maximum_y, point.y);
  }
  minimum_y = std::max<std::int32_t>(minimum_y, 0);
  maximum_y = std::min<std::int32_t>(
      maximum_y, static_cast<std::int32_t>(destination.height()));
  std::vector<std::int64_t> crossings;
  crossings.reserve(points.size());
  for (auto y = minimum_y; y < maximum_y; ++y) {
    crossings.clear();
    const auto scan_y = static_cast<std::int64_t>(y) * 2 + 1;
    for (std::size_t index = 0; index < points.size(); ++index) {
      const auto a = points[index];
      const auto b = points[(index + 1) % points.size()];
      const auto ay = static_cast<std::int64_t>(a.y) * 2;
      const auto by = static_cast<std::int64_t>(b.y) * 2;
      if ((ay <= scan_y && scan_y < by) ||
          (by <= scan_y && scan_y < ay)) {
        const auto numerator =
            (scan_y - ay) * (static_cast<std::int64_t>(b.x) - a.x);
        crossings.push_back(static_cast<std::int64_t>(a.x) * 2 +
                            numerator * 2 / (by - ay));
      }
    }
    std::sort(crossings.begin(), crossings.end());
    for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
      const auto left = crossings[index];
      const auto right = crossings[index + 1];
      const auto first_x = static_cast<std::int32_t>((left + 1) / 2);
      const auto last_x = static_cast<std::int32_t>((right - 1) / 2);
      for (auto x = std::max(first_x, 0);
           x <= last_x && static_cast<std::uint32_t>(x) < destination.width();
           ++x)
        plot(destination, x, y, foreground, plane_mask);
    }
  }
}

void fill_ellipse(PixelStorage& destination, const RasterEllipse ellipse,
                  const std::uint32_t foreground,
                  const std::uint32_t plane_mask) noexcept {
  if (ellipse.width == 0 || ellipse.height == 0) return;
  const auto minimum_x = std::max<std::int64_t>(ellipse.x, 0);
  const auto minimum_y = std::max<std::int64_t>(ellipse.y, 0);
  const auto maximum_x = std::min<std::int64_t>(
      static_cast<std::int64_t>(ellipse.x) + ellipse.width,
      destination.width());
  const auto maximum_y = std::min<std::int64_t>(
      static_cast<std::int64_t>(ellipse.y) + ellipse.height,
      destination.height());
  if (minimum_x >= maximum_x || minimum_y >= maximum_y) return;
  const auto width = static_cast<std::uint64_t>(ellipse.width);
  const auto height = static_cast<std::uint64_t>(ellipse.height);
  const auto rhs = width * width * height * height;
  for (auto y = minimum_y; y < maximum_y; ++y) {
    const auto dy = 2 * (y - ellipse.y) + 1 -
                    static_cast<std::int64_t>(height);
    const auto dy_squared = static_cast<std::uint64_t>(dy * dy);
    for (auto x = minimum_x; x < maximum_x; ++x) {
      const auto dx = 2 * (x - ellipse.x) + 1 -
                      static_cast<std::int64_t>(width);
      const auto x_term = static_cast<std::uint64_t>(dx * dx) * height * height;
      const auto y_term = dy_squared * width * width;
      if (x_term <= rhs && y_term <= rhs - x_term)
        plot(destination, static_cast<std::int32_t>(x),
             static_cast<std::int32_t>(y), foreground, plane_mask);
    }
  }
}

}  // namespace glasswyrm::server
