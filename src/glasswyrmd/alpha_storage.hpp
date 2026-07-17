#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace glasswyrm::server {

// Canonical depth-eight alpha storage. One byte per pixel keeps A8 independent
// of client request byte order and avoids conflating alpha with indexed color.
class AlphaStorage {
 public:
  static constexpr std::uint32_t kMaximumDimension = 16384;
  static constexpr std::size_t kMaximumBytes = 64U * 1024U * 1024U;

  [[nodiscard]] static std::optional<AlphaStorage> create(
      std::uint32_t width, std::uint32_t height,
      std::uint8_t initial_alpha = 0);
  [[nodiscard]] std::optional<AlphaStorage> resize_preserving_overlap(
      std::uint32_t width, std::uint32_t height,
      std::uint8_t initial_alpha = 0) const;

  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t stride() const noexcept { return width_; }
  [[nodiscard]] std::size_t byte_size() const noexcept { return alpha_.size(); }
  [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return alpha_; }
  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
    return alpha_;
  }
  [[nodiscard]] std::uint8_t& at(std::uint32_t x,
                                 std::uint32_t y) noexcept {
    return alpha_[static_cast<std::size_t>(y) * width_ + x];
  }
  [[nodiscard]] std::uint8_t at(std::uint32_t x,
                                std::uint32_t y) const noexcept {
    return alpha_[static_cast<std::size_t>(y) * width_ + x];
  }
  void fill(geometry::Rectangle rectangle, std::uint8_t alpha) noexcept;

 private:
  AlphaStorage(std::uint32_t width, std::uint32_t height,
               std::vector<std::uint8_t> alpha)
      : width_(width), height_(height), alpha_(std::move(alpha)) {}

  std::uint32_t width_{};
  std::uint32_t height_{};
  std::vector<std::uint8_t> alpha_;
};

}  // namespace glasswyrm::server
