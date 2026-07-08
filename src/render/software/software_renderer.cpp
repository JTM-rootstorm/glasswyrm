#include <glasswyrm/render/software_renderer.hpp>

#include <algorithm>
#include <stdexcept>

namespace glasswyrm::render {

SoftwareFramebuffer::SoftwareFramebuffer(std::uint32_t width,
                                         std::uint32_t height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height) {
  if (width == 0 || height == 0) {
    throw std::invalid_argument("software framebuffer dimensions must be non-zero");
  }
}

std::uint32_t SoftwareFramebuffer::width() const noexcept {
  return width_;
}

std::uint32_t SoftwareFramebuffer::height() const noexcept {
  return height_;
}

const std::vector<Pixel>& SoftwareFramebuffer::pixels() const noexcept {
  return pixels_;
}

void SoftwareFramebuffer::clear(Pixel color) {
  std::fill(pixels_.begin(), pixels_.end(), color);
}

}  // namespace glasswyrm::render
