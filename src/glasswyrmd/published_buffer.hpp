#pragma once

#include "core/geometry/rectangle.hpp"
#include "glasswyrmd/pixel_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>

namespace glasswyrm::server {

enum class PublishedBufferRetirement { Replaced, SurfaceRemoved };

class PublishedWindowBuffer {
 public:
  static std::unique_ptr<PublishedWindowBuffer> create(
      std::uint64_t buffer_id, std::uint32_t window_xid,
      const PixelStorage& canonical);

  ~PublishedWindowBuffer();
  PublishedWindowBuffer(const PublishedWindowBuffer&) = delete;
  PublishedWindowBuffer& operator=(const PublishedWindowBuffer&) = delete;
  PublishedWindowBuffer(PublishedWindowBuffer&&) = delete;
  PublishedWindowBuffer& operator=(PublishedWindowBuffer&&) = delete;

  [[nodiscard]] std::uint64_t buffer_id() const noexcept { return buffer_id_; }
  [[nodiscard]] std::uint32_t window_xid() const noexcept { return window_xid_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] int fd() const noexcept { return fd_; }
  [[nodiscard]] bool announced() const noexcept { return announced_; }
  void set_announced(bool announced) noexcept { announced_ = announced; }

  [[nodiscard]] bool copy_from(
      const PixelStorage& canonical,
      std::span<const geometry::Rectangle> rectangles) noexcept;
  [[nodiscard]] bool copy_all_from(const PixelStorage& canonical) noexcept;

 private:
  PublishedWindowBuffer(std::uint64_t buffer_id, std::uint32_t window_xid,
                        std::uint32_t width, std::uint32_t height,
                        std::uint32_t stride, std::size_t size, int fd,
                        void* mapping) noexcept;

  std::uint64_t buffer_id_{};
  std::uint32_t window_xid_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::uint32_t stride_{};
  std::size_t size_{};
  int fd_{-1};
  void* mapping_{nullptr};
  bool announced_{};
};

class PublishedBufferStore {
 public:
  static constexpr std::size_t kMaximumBytes = 256U * 1024U * 1024U;

  explicit PublishedBufferStore(std::size_t maximum_bytes = kMaximumBytes)
      : maximum_bytes_(maximum_bytes) {}

  [[nodiscard]] std::uint64_t next_buffer_id() noexcept;
  [[nodiscard]] bool install(std::uint32_t window_xid,
                             std::unique_ptr<PublishedWindowBuffer> buffer);
  [[nodiscard]] PublishedWindowBuffer* current(std::uint32_t window_xid) noexcept;
  [[nodiscard]] const PublishedWindowBuffer* current(
      std::uint32_t window_xid) const noexcept;
  [[nodiscard]] bool retire(std::uint32_t window_xid,
                            PublishedBufferRetirement reason);
  [[nodiscard]] bool release(std::uint64_t buffer_id,
                             PublishedBufferRetirement reason);
  void peer_disconnected() noexcept;
  [[nodiscard]] std::size_t accounted_bytes() const noexcept {
    return accounted_bytes_;
  }

 private:
  struct Retired {
    PublishedBufferRetirement reason;
    std::unique_ptr<PublishedWindowBuffer> buffer;
  };
  std::uint64_t next_id_{1};
  std::size_t maximum_bytes_;
  std::size_t accounted_bytes_{};
  std::map<std::uint32_t, std::unique_ptr<PublishedWindowBuffer>> current_;
  std::map<std::uint64_t, Retired> retired_;
};

}  // namespace glasswyrm::server
