#include "backends/drm/kms_api.hpp"

namespace glasswyrm::drm {
bool KmsDumbBufferApi::create_dumb(std::uint32_t w, std::uint32_t h,
                                   std::uint32_t bpp, DumbAllocation &out,
                                   std::string &error) {
  return api_.create_dumb(fd_, w, h, bpp, out, error);
}
bool KmsDumbBufferApi::add_framebuffer2(std::uint32_t handle, std::uint32_t w,
                                        std::uint32_t h, std::uint32_t pitch,
                                        std::uint32_t format, std::uint32_t &fb,
                                        std::string &error) {
  return api_.add_framebuffer2(fd_, handle, w, h, pitch, format, fb, error);
}
bool KmsDumbBufferApi::map_dumb(std::uint32_t handle, std::uint64_t &offset,
                                std::string &error) {
  return api_.map_dumb(fd_, handle, offset, error);
}
std::byte *KmsDumbBufferApi::map_memory(std::uint64_t offset, std::size_t size,
                                        std::string &error) {
  return api_.map_memory(fd_, offset, size, error);
}
bool KmsDumbBufferApi::remove_framebuffer(std::uint32_t fb,
                                          std::string &error) noexcept {
  return api_.remove_framebuffer(fd_, fb, error);
}
bool KmsDumbBufferApi::unmap_memory(std::byte *map, std::size_t size,
                                    std::string &error) noexcept {
  return api_.unmap_memory(map, size, error);
}
bool KmsDumbBufferApi::destroy_dumb(std::uint32_t handle,
                                    std::string &error) noexcept {
  return api_.destroy_dumb(fd_, handle, error);
}
} // namespace glasswyrm::drm
