#include "backends/headless/output.hpp"

#include <limits>

namespace glasswyrm::headless {

bool Output::configure(const std::uint64_t id, const std::uint32_t width,
                       const std::uint32_t height, std::string& error) {
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
  if (pixels > kMaximumPixels || pixels > kMaximumBytes / sizeof(std::uint32_t) ||
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

void Output::disable() noexcept {
  id_ = 0;
  width_ = 0;
  height_ = 0;
  enabled_ = false;
  pixels_.clear();
}

}  // namespace glasswyrm::headless
