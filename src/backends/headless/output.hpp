#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::headless {

class Output {
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

}  // namespace glasswyrm::headless
