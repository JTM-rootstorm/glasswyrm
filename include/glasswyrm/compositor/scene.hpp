#pragma once

#include <cstdint>
#include <string_view>

namespace glasswyrm::compositor {

struct Extent {
  std::uint32_t width;
  std::uint32_t height;
};

struct HeadlessOutput {
  std::string_view name;
  Extent physical_size;
  float logical_scale;
};

class Scene {
 public:
  explicit Scene(HeadlessOutput output);

  [[nodiscard]] const HeadlessOutput& output() const noexcept;
  [[nodiscard]] bool is_headless() const noexcept;
  [[nodiscard]] std::uint64_t frame_number() const noexcept;

  void mark_frame_presented() noexcept;

 private:
  HeadlessOutput output_;
  std::uint64_t frame_number_ = 0;
};

HeadlessOutput default_headless_output() noexcept;

}  // namespace glasswyrm::compositor
