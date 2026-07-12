#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

class PixelStorage {
 public:
  static constexpr std::uint32_t kOpaqueBlack = 0xff000000U;
  static constexpr std::uint32_t kMaximumDimension = 16384;
  static constexpr std::size_t kMaximumBytes = 64U * 1024U * 1024U;
  [[nodiscard]] static std::optional<PixelStorage> create(std::uint32_t width,
                                                          std::uint32_t height);
  [[nodiscard]] std::optional<PixelStorage> resize_preserving_overlap(
      std::uint32_t width, std::uint32_t height,
      std::uint32_t initial = kOpaqueBlack) const;
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t stride() const noexcept { return width_ * 4U; }
  [[nodiscard]] std::size_t byte_size() const noexcept { return pixels_.size() * 4U; }
  [[nodiscard]] std::span<std::uint32_t> pixels() noexcept { return pixels_; }
  [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept { return pixels_; }
  [[nodiscard]] std::uint32_t& at(std::uint32_t x, std::uint32_t y) noexcept {
    return pixels_[static_cast<std::size_t>(y) * width_ + x];
  }
  [[nodiscard]] std::uint32_t at(std::uint32_t x, std::uint32_t y) const noexcept {
    return pixels_[static_cast<std::size_t>(y) * width_ + x];
  }
  void fill(glasswyrm::geometry::Rectangle rectangle, std::uint32_t rgb,
            std::uint32_t plane_mask = 0x00ffffffU) noexcept;
 private:
  PixelStorage(std::uint32_t width, std::uint32_t height,
               std::vector<std::uint32_t> pixels)
      : width_(width), height_(height), pixels_(std::move(pixels)) {}
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::vector<std::uint32_t> pixels_;
};

}  // namespace glasswyrm::server
