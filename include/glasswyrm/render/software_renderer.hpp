#pragma once

#include <cstdint>
#include <vector>

namespace glasswyrm::render {

struct Pixel {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;
};

class SoftwareFramebuffer {
 public:
  SoftwareFramebuffer(std::uint32_t width, std::uint32_t height);

  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] const std::vector<Pixel>& pixels() const noexcept;

  void clear(Pixel color);

 private:
  std::uint32_t width_;
  std::uint32_t height_;
  std::vector<Pixel> pixels_;
};

}  // namespace glasswyrm::render
