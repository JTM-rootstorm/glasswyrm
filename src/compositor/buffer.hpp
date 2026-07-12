#pragma once

#include <glasswyrm/ipc/contracts.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gw::compositor {

class BufferMapping final {
public:
  static constexpr std::uint64_t kMaximumMappingBytes = 64ULL * 1024ULL * 1024ULL;

  // Takes ownership of fd on every path.
  [[nodiscard]] static std::unique_ptr<BufferMapping>
  import(const gwipc_buffer_attach& attachment, int fd, std::string& error);

  ~BufferMapping();
  BufferMapping(const BufferMapping&) = delete;
  BufferMapping& operator=(const BufferMapping&) = delete;
  BufferMapping(BufferMapping&&) = delete;
  BufferMapping& operator=(BufferMapping&&) = delete;

  [[nodiscard]] std::uint64_t id() const noexcept { return buffer_id_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }
  [[nodiscard]] gwipc_pixel_format pixel_format() const noexcept { return pixel_format_; }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {static_cast<const std::byte*>(mapping_), mapping_size_};
  }

private:
  BufferMapping(const gwipc_buffer_attach& attachment, void* mapping,
                std::size_t mapping_size) noexcept;

  std::uint64_t buffer_id_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::uint32_t stride_{};
  gwipc_pixel_format pixel_format_{};
  void* mapping_{};
  std::size_t mapping_size_{};
};

} // namespace gw::compositor
