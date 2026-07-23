#include "backends/output/software_frame.hpp"

#include <chrono>
#include <limits>

namespace glasswyrm::output {

FrameHashMeasurement hash_visible_xrgb8888_measured(
    const std::span<const std::uint32_t> pixels) noexcept {
  const auto started = std::chrono::steady_clock::now();
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto pixel : pixels) {
    const std::uint8_t bytes[3] = {
        static_cast<std::uint8_t>(pixel >> 16U),
        static_cast<std::uint8_t>(pixel >> 8U),
        static_cast<std::uint8_t>(pixel)};
    for (const auto byte : bytes) {
      hash ^= byte;
      hash *= 1099511628211ULL;
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started);
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto bytes = pixels.size() > maximum / 3U
                         ? maximum
                         : static_cast<std::uint64_t>(pixels.size()) * 3U;
  return {hash, bytes,
          elapsed.count() < 0 ? 0U
                              : static_cast<std::uint64_t>(elapsed.count())};
}

std::uint64_t hash_visible_xrgb8888(
    const std::span<const std::uint32_t> pixels) noexcept {
  return hash_visible_xrgb8888_measured(pixels).hash;
}

bool SoftwareFrame::configure(const std::uint64_t id,
                              const std::uint32_t width,
                              const std::uint32_t height,
                              std::string& error) {
  if (id == 0) {
    error = "headless output ID must be nonzero";
    return false;
  }
  if (width == 0 || height == 0 || width > kMaximumWidth ||
      height > kMaximumHeight) {
    error = "headless output dimensions are outside supported limits";
    return false;
  }

  const auto pixels = static_cast<std::uint64_t>(width) * height;
  if (pixels > kMaximumPixels ||
      pixels > kMaximumBytes / sizeof(std::uint32_t) ||
      pixels > std::numeric_limits<std::size_t>::max()) {
    error = "headless framebuffer exceeds supported limits";
    return false;
  }

  try {
    std::vector<std::uint32_t> replacement(static_cast<std::size_t>(pixels),
                                           kClearPixel);
    pixels_.swap(replacement);
  } catch (...) {
    error = "headless framebuffer allocation failed";
    return false;
  }

  id_ = id;
  width_ = width;
  height_ = height;
  enabled_ = true;
  error.clear();
  return true;
}

void SoftwareFrame::disable() noexcept {
  id_ = 0;
  width_ = 0;
  height_ = 0;
  enabled_ = false;
  pixels_.clear();
}

OutputSpec SoftwareFrame::spec(
    const std::uint32_t refresh_millihz) const noexcept {
  return {id_, width_, height_, refresh_millihz};
}

}  // namespace glasswyrm::output
