#pragma once

#include "backends/drm/dumb_buffer_api.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace glasswyrm::drm {

class DumbBuffer {
 public:
  static constexpr std::uint64_t kMaximumBytes = 64U * 1024U * 1024U;

  DumbBuffer() = default;
  ~DumbBuffer();
  DumbBuffer(DumbBuffer&& other) noexcept;
  DumbBuffer& operator=(DumbBuffer&& other) noexcept;
  DumbBuffer(const DumbBuffer&) = delete;
  DumbBuffer& operator=(const DumbBuffer&) = delete;

  [[nodiscard]] static bool create(DumbBufferApi& api, std::uint32_t width,
                                   std::uint32_t height, DumbBuffer& output,
                                   std::string& error);
  [[nodiscard]] bool copy_from(std::span<const std::uint32_t> pixels,
                               std::string& error);
  [[nodiscard]] std::uint64_t visible_hash() const noexcept;
  void reset() noexcept;

  [[nodiscard]] bool valid() const noexcept { return mapping_ != nullptr; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t pitch() const noexcept { return pitch_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::uint32_t handle() const noexcept { return handle_; }
  [[nodiscard]] std::uint32_t framebuffer_id() const noexcept {
    return framebuffer_id_;
  }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {mapping_, size_};
  }

 private:
  DumbBufferApi* api_{};
  std::byte* mapping_{};
  std::size_t size_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::uint32_t pitch_{};
  std::uint32_t handle_{};
  std::uint32_t framebuffer_id_{};
};

class DumbBufferPair {
 public:
  DumbBufferPair() = default;
  DumbBufferPair(DumbBufferPair&&) noexcept = default;
  DumbBufferPair& operator=(DumbBufferPair&&) noexcept = default;
  DumbBufferPair(const DumbBufferPair&) = delete;
  DumbBufferPair& operator=(const DumbBufferPair&) = delete;

  [[nodiscard]] static bool create(DumbBufferApi& api, std::uint32_t width,
                                   std::uint32_t height,
                                   DumbBufferPair& output,
                                   std::string& error);
  [[nodiscard]] DumbBuffer& front() noexcept { return buffers_[front_index_]; }
  [[nodiscard]] const DumbBuffer& front() const noexcept {
    return buffers_[front_index_];
  }
  [[nodiscard]] DumbBuffer& back() noexcept { return buffers_[1 - front_index_]; }
  [[nodiscard]] const DumbBuffer& back() const noexcept {
    return buffers_[1 - front_index_];
  }
  void promote_back() noexcept { front_index_ = 1 - front_index_; }

 private:
  std::array<DumbBuffer, 2> buffers_;
  std::size_t front_index_{};
};

}  // namespace glasswyrm::drm
