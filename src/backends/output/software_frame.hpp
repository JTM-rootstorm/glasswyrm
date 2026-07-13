#pragma once

#include "compositor/rectangle.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::output {

struct OutputSpec {
  std::uint64_t output_id{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t refresh_millihz{};
};

struct SoftwareFrameView {
  OutputSpec output;
  std::span<const std::uint32_t> pixels;
  std::span<const gw::compositor::Rectangle> damage;
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t ordinal{};
};

[[nodiscard]] std::uint64_t hash_visible_xrgb8888(
    std::span<const std::uint32_t> pixels) noexcept;

class SoftwareFrame {
 public:
  static constexpr std::uint32_t kMaximumWidth = 4096;
  static constexpr std::uint32_t kMaximumHeight = 4096;
  static constexpr std::uint64_t kMaximumPixels = 16'777'216;
  static constexpr std::uint64_t kMaximumBytes = 67'108'864;
  static constexpr std::uint32_t kClearPixel = 0xff000000U;

  [[nodiscard]] bool configure(std::uint64_t id, std::uint32_t width,
                               std::uint32_t height, std::string& error);
  void disable() noexcept;

  [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] OutputSpec spec(
      std::uint32_t refresh_millihz = 0) const noexcept;
  [[nodiscard]] std::uint64_t visible_hash() const noexcept {
    return hash_visible_xrgb8888(pixels_);
  }
  [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept {
    return pixels_;
  }
  [[nodiscard]] std::span<std::uint32_t> pixels() noexcept { return pixels_; }

 private:
  std::uint64_t id_{0};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  bool enabled_{false};
  std::vector<std::uint32_t> pixels_;
};

}  // namespace glasswyrm::output
