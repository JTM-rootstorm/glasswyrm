#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

// Canonical depth-one storage. One byte per pixel deliberately keeps the
// server-side representation independent of client bitmap byte order.
class BitmapStorage {
 public:
  static constexpr std::uint32_t kMaximumDimension = 16384;
  static constexpr std::size_t kMaximumBytes = 64U * 1024U * 1024U;

  [[nodiscard]] static std::optional<BitmapStorage> create(
      std::uint32_t width, std::uint32_t height);
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::size_t byte_size() const noexcept { return bits_.size(); }
  [[nodiscard]] std::span<std::uint8_t> bits() noexcept { return bits_; }
  [[nodiscard]] std::span<const std::uint8_t> bits() const noexcept {
    return bits_;
  }
  [[nodiscard]] std::uint8_t at(std::uint32_t x,
                                std::uint32_t y) const noexcept {
    return bits_[static_cast<std::size_t>(y) * width_ + x];
  }
  void set(std::uint32_t x, std::uint32_t y, std::uint8_t value) noexcept {
    bits_[static_cast<std::size_t>(y) * width_ + x] = value & 1U;
  }

 private:
  BitmapStorage(std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t> bits)
      : width_(width), height_(height), bits_(std::move(bits)) {}

  std::uint32_t width_{};
  std::uint32_t height_{};
  std::vector<std::uint8_t> bits_;
};

[[nodiscard]] bool put_xybitmap_lsb32(
    BitmapStorage& destination, std::int32_t destination_x,
    std::int32_t destination_y, std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> payload, std::uint32_t foreground,
    std::uint32_t background, std::uint32_t plane_mask) noexcept;

[[nodiscard]] bool put_zpixmap_lsb32(
    BitmapStorage& destination, std::int32_t destination_x,
    std::int32_t destination_y, std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> payload,
    std::uint32_t plane_mask) noexcept;

}  // namespace glasswyrm::server
