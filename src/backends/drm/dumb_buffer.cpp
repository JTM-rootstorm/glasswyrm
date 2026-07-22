#include "backends/drm/dumb_buffer.hpp"

#include "backends/drm/resources.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace glasswyrm::drm {
namespace {

constexpr std::uint32_t kBytesPerPixel = 4;

bool validate_allocation(const DumbAllocation &allocation,
                         const std::uint32_t width, const std::uint32_t height,
                         std::size_t &mapped_size, std::string &error) {
  if (allocation.handle == 0) {
    error = "DRM dumb-buffer creation returned a zero handle";
    return false;
  }
  if (width > std::numeric_limits<std::uint32_t>::max() / kBytesPerPixel) {
    error = "DRM dumb-buffer row width overflows";
    return false;
  }
  const auto visible_row_bytes = width * kBytesPerPixel;
  if (allocation.pitch < visible_row_bytes) {
    error = "DRM dumb-buffer pitch is smaller than the visible row";
    return false;
  }
  if (height != 0 &&
      allocation.pitch > std::numeric_limits<std::uint64_t>::max() / height) {
    error = "DRM dumb-buffer pitched size overflows";
    return false;
  }
  const auto required_size =
      static_cast<std::uint64_t>(allocation.pitch) * height;
  if (allocation.size < required_size) {
    error = "DRM dumb-buffer size is smaller than pitch times height";
    return false;
  }
  if (allocation.size == 0 || allocation.size > DumbBuffer::kMaximumBytes ||
      allocation.size > std::numeric_limits<std::size_t>::max()) {
    error = "DRM dumb-buffer size exceeds the 64 MiB output limit";
    return false;
  }
  mapped_size = static_cast<std::size_t>(allocation.size);
  return true;
}

} // namespace

DumbBuffer::~DumbBuffer() { reset(); }

DumbBuffer::DumbBuffer(DumbBuffer &&other) noexcept {
  *this = std::move(other);
}

DumbBuffer &DumbBuffer::operator=(DumbBuffer &&other) noexcept {
  if (this == &other)
    return *this;
  reset();
  api_ = std::exchange(other.api_, nullptr);
  mapping_ = std::exchange(other.mapping_, nullptr);
  size_ = std::exchange(other.size_, 0);
  width_ = std::exchange(other.width_, 0);
  height_ = std::exchange(other.height_, 0);
  pitch_ = std::exchange(other.pitch_, 0);
  handle_ = std::exchange(other.handle_, 0);
  framebuffer_id_ = std::exchange(other.framebuffer_id_, 0);
  completed_generation_ = std::exchange(other.completed_generation_, 0);
  content_valid_ = std::exchange(other.content_valid_, false);
  visible_hash_ = std::exchange(other.visible_hash_, std::nullopt);
  return *this;
}

bool DumbBuffer::create(DumbBufferApi &api, const std::uint32_t width,
                        const std::uint32_t height, DumbBuffer &output,
                        std::string &error) {
  if (width == 0 || height == 0) {
    error = "DRM dumb-buffer dimensions must be nonzero";
    return false;
  }
  DumbAllocation allocation;
  if (!api.create_dumb(width, height, 32, allocation, error))
    return false;

  DumbBuffer replacement;
  replacement.api_ = &api;
  replacement.handle_ = allocation.handle;
  if (!validate_allocation(allocation, width, height, replacement.size_, error))
    return false;
  replacement.width_ = width;
  replacement.height_ = height;
  replacement.pitch_ = allocation.pitch;
  if (!api.add_framebuffer2(allocation.handle, width, height, allocation.pitch,
                            kFormatXrgb8888, replacement.framebuffer_id_,
                            error))
    return false;
  if (replacement.framebuffer_id_ == 0) {
    error = "DRM AddFB2 returned a zero framebuffer ID";
    return false;
  }
  std::uint64_t map_offset = 0;
  if (!api.map_dumb(allocation.handle, map_offset, error))
    return false;
  replacement.mapping_ = api.map_memory(map_offset, replacement.size_, error);
  if (replacement.mapping_ == nullptr)
    return false;
  std::memset(replacement.mapping_, 0, replacement.size_);
  output = std::move(replacement);
  error.clear();
  return true;
}

bool DumbBuffer::copy_from(const std::span<const std::uint32_t> pixels,
                           std::string &error) {
  const auto pixel_count = static_cast<std::uint64_t>(width_) * height_;
  if (!valid() || pixel_count != pixels.size()) {
    error = "canonical software frame does not match the DRM dumb buffer";
    return false;
  }
  visible_hash_.reset();
  std::memset(mapping_, 0, size_);
  const auto row_bytes = static_cast<std::size_t>(width_) * kBytesPerPixel;
  for (std::uint32_t row = 0; row < height_; ++row) {
    std::memcpy(mapping_ + static_cast<std::size_t>(row) * pitch_,
                pixels.data() + static_cast<std::size_t>(row) * width_,
                row_bytes);
  }
  error.clear();
  return true;
}

bool DumbBuffer::copy_rectangles_from(
    const std::span<const std::uint32_t> pixels,
    const std::span<const gw::compositor::Rectangle> rectangles,
    std::string &error) {
  const auto pixel_count = static_cast<std::uint64_t>(width_) * height_;
  if (!valid() || !content_valid_ || pixel_count != pixels.size()) {
    error = "partial copy requires a valid matching DRM dumb buffer";
    return false;
  }
  for (const auto& rectangle : rectangles) {
    const auto right = std::int64_t{rectangle.x} + rectangle.width;
    const auto bottom = std::int64_t{rectangle.y} + rectangle.height;
    if (rectangle.empty() || rectangle.x < 0 || rectangle.y < 0 ||
        right > width_ || bottom > height_) {
      error = "partial DRM copy rectangle exceeds the visible buffer";
      return false;
    }
  }
  visible_hash_.reset();
  for (const auto& rectangle : rectangles) {
    const auto bytes = static_cast<std::size_t>(rectangle.width) *
                       kBytesPerPixel;
    for (std::uint32_t row = 0; row < rectangle.height; ++row) {
      const auto y = static_cast<std::uint32_t>(rectangle.y) + row;
      const auto x = static_cast<std::uint32_t>(rectangle.x);
      std::memcpy(mapping_ + static_cast<std::size_t>(y) * pitch_ +
                      static_cast<std::size_t>(x) * kBytesPerPixel,
                  pixels.data() + static_cast<std::size_t>(y) * width_ + x,
                  bytes);
    }
  }
  error.clear();
  return true;
}

std::uint64_t DumbBuffer::visible_hash() const noexcept {
  if (!valid())
    return 0;
  if (visible_hash_)
    return *visible_hash_;
  std::uint64_t hash = 14695981039346656037ULL;
  for (std::uint32_t row = 0; row < height_; ++row) {
    const auto *row_bytes = mapping_ + static_cast<std::size_t>(row) * pitch_;
    for (std::uint32_t column = 0; column < width_; ++column) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel, row_bytes + static_cast<std::size_t>(column) * 4,
                  sizeof(pixel));
      const std::uint8_t rgb[3] = {static_cast<std::uint8_t>(pixel >> 16U),
                                   static_cast<std::uint8_t>(pixel >> 8U),
                                   static_cast<std::uint8_t>(pixel)};
      for (const auto byte : rgb) {
        hash ^= byte;
        hash *= 1099511628211ULL;
      }
    }
  }
  visible_hash_ = hash;
  return *visible_hash_;
}

bool DumbBuffer::verify_visible_pixels(
    const std::span<const std::uint32_t> pixels,
    const std::uint64_t verified_hash) const noexcept {
  visible_hash_.reset();
  const auto pixel_count = static_cast<std::uint64_t>(width_) * height_;
  if (!valid() || pixel_count != pixels.size())
    return false;
  const auto row_bytes = static_cast<std::size_t>(width_) * kBytesPerPixel;
  for (std::uint32_t row = 0; row < height_; ++row) {
    if (std::memcmp(mapping_ + static_cast<std::size_t>(row) * pitch_,
                    pixels.data() + static_cast<std::size_t>(row) * width_,
                    row_bytes) != 0)
      return false;
  }
  visible_hash_ = verified_hash;
  return true;
}

void DumbBuffer::reset() noexcept {
  std::string ignored;
  (void)release(ignored);
}

bool DumbBuffer::release(std::string &error) noexcept {
  error.clear();
  if (api_ == nullptr)
    return true;
  bool success = true;
  std::string operation_error;
  const auto append_error = [&error](const std::string &detail) {
    if (!error.empty())
      error += "; ";
    error += detail;
  };
  if (framebuffer_id_ != 0 &&
      !api_->remove_framebuffer(framebuffer_id_, operation_error)) {
    append_error(operation_error.empty() ? "remove DRM framebuffer"
                                         : operation_error);
    success = false;
  }
  operation_error.clear();
  if (mapping_ != nullptr &&
      !api_->unmap_memory(mapping_, size_, operation_error)) {
    append_error(operation_error.empty() ? "unmap DRM dumb buffer"
                                         : operation_error);
    success = false;
  }
  operation_error.clear();
  if (handle_ != 0 && !api_->destroy_dumb(handle_, operation_error)) {
    append_error(operation_error.empty() ? "destroy DRM dumb buffer"
                                         : operation_error);
    success = false;
  }
  api_ = nullptr;
  mapping_ = nullptr;
  size_ = 0;
  width_ = 0;
  height_ = 0;
  pitch_ = 0;
  handle_ = 0;
  framebuffer_id_ = 0;
  completed_generation_ = 0;
  content_valid_ = false;
  visible_hash_.reset();
  return success;
}

void DumbBuffer::abandon() noexcept {
  api_ = nullptr;
  mapping_ = nullptr;
  size_ = 0;
  width_ = 0;
  height_ = 0;
  pitch_ = 0;
  handle_ = 0;
  framebuffer_id_ = 0;
  completed_generation_ = 0;
  content_valid_ = false;
  visible_hash_.reset();
}

bool DumbBufferPair::create(DumbBufferApi &api, const std::uint32_t width,
                            const std::uint32_t height, DumbBufferPair &output,
                            std::string &error) {
  DumbBufferPair replacement;
  for (auto &buffer : replacement.buffers_)
    if (!DumbBuffer::create(api, width, height, buffer, error))
      return false;
  output = std::move(replacement);
  return true;
}

bool DumbBufferPair::release(std::string &error) noexcept {
  error.clear();
  bool success = true;
  for (auto &buffer : buffers_) {
    std::string buffer_error;
    if (buffer.release(buffer_error))
      continue;
    if (!error.empty())
      error += "; ";
    error += buffer_error;
    success = false;
  }
  front_index_ = 0;
  return success;
}

void DumbBufferPair::abandon() noexcept {
  for (auto &buffer : buffers_)
    buffer.abandon();
  front_index_ = 0;
}

} // namespace glasswyrm::drm
