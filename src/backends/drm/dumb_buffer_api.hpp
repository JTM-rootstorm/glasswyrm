#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace glasswyrm::drm {

struct DumbAllocation {
  std::uint32_t handle{};
  std::uint32_t pitch{};
  std::uint64_t size{};
};

class DumbBufferApi {
 public:
  virtual ~DumbBufferApi() = default;

  [[nodiscard]] virtual bool create_dumb(std::uint32_t width,
                                         std::uint32_t height,
                                         std::uint32_t bits_per_pixel,
                                         DumbAllocation& allocation,
                                         std::string& error) = 0;
  [[nodiscard]] virtual bool add_framebuffer2(
      std::uint32_t handle, std::uint32_t width, std::uint32_t height,
      std::uint32_t pitch, std::uint32_t format,
      std::uint32_t& framebuffer_id, std::string& error) = 0;
  [[nodiscard]] virtual bool map_dumb(std::uint32_t handle,
                                     std::uint64_t& offset,
                                     std::string& error) = 0;
  [[nodiscard]] virtual std::byte* map_memory(std::uint64_t offset,
                                             std::size_t size,
                                             std::string& error) = 0;
  virtual void remove_framebuffer(std::uint32_t framebuffer_id) noexcept = 0;
  virtual void unmap_memory(std::byte* mapping, std::size_t size) noexcept = 0;
  virtual void destroy_dumb(std::uint32_t handle) noexcept = 0;
};

}  // namespace glasswyrm::drm
