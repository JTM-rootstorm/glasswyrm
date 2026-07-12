#include "compositor/buffer.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gw::compositor {
namespace {

class OwnedFd final {
public:
  explicit OwnedFd(int fd) noexcept : fd_(fd) {}
  ~OwnedFd() { if (fd_ >= 0) (void)::close(fd_); }
  OwnedFd(const OwnedFd&) = delete;
  OwnedFd& operator=(const OwnedFd&) = delete;
  [[nodiscard]] int get() const noexcept { return fd_; }
private:
  int fd_;
};

[[nodiscard]] bool is_srgb(const gwipc_sdr_color_metadata& color) noexcept {
  return color.color_space == GWIPC_SDR_COLOR_SPACE_SRGB &&
         color.transfer_function == GWIPC_TRANSFER_FUNCTION_SRGB &&
         color.primaries == GWIPC_COLOR_PRIMARIES_SRGB &&
         color.luminance_available == 0 && color.minimum_luminance_millinit == 0 &&
         color.maximum_luminance_millinit == 0 &&
         color.max_frame_average_luminance_millinit == 0;
}

[[nodiscard]] bool metadata_valid(const gwipc_buffer_attach& value,
                                  std::string& error) {
  if (value.struct_size < sizeof(gwipc_buffer_attach)) { error = "buffer attachment structure is truncated"; return false; }
  if (value.buffer_id == 0 || value.width == 0 || value.height == 0) { error = "buffer id and geometry must be nonzero"; return false; }
  if (value.byte_offset != 0) { error = "nonzero buffer offsets are unsupported"; return false; }
  if (value.modifier != 0) { error = "only linear buffers are supported"; return false; }
  if (value.synchronization != GWIPC_SYNCHRONIZATION_NONE || value.flags != 0) { error = "unsupported synchronization mode or flags"; return false; }
  if (!is_srgb(value.color)) { error = "only SDR sRGB buffer metadata is supported"; return false; }
  const bool xrgb = value.pixel_format == GWIPC_PIXEL_FORMAT_XRGB8888 && value.alpha_semantics == GWIPC_ALPHA_OPAQUE;
  const bool argb = value.pixel_format == GWIPC_PIXEL_FORMAT_ARGB8888 && value.alpha_semantics == GWIPC_ALPHA_PREMULTIPLIED;
  if (!xrgb && !argb) { error = "pixel format and alpha semantics are incompatible"; return false; }

  const std::uint64_t row_bytes = static_cast<std::uint64_t>(value.width) * 4U;
  if (value.stride < row_bytes || value.stride % 4U != 0) { error = "buffer stride is invalid"; return false; }
  const std::uint64_t rows = static_cast<std::uint64_t>(value.height - 1U);
  if (rows != 0 && value.stride > (std::numeric_limits<std::uint64_t>::max() - row_bytes) / rows) { error = "buffer geometry overflows storage calculation"; return false; }
  const std::uint64_t required = rows * value.stride + row_bytes;
  if (required > value.storage_size) { error = "declared buffer storage is too small"; return false; }
  if (value.storage_size == 0 || value.storage_size > BufferMapping::kMaximumMappingBytes ||
      value.storage_size > std::numeric_limits<std::size_t>::max()) { error = "buffer mapping size is unsupported"; return false; }
  return true;
}

} // namespace

std::unique_ptr<BufferMapping> BufferMapping::import(
    const gwipc_buffer_attach& attachment, int fd, std::string& error) {
  error.clear();
  OwnedFd owned(fd);
  if (fd < 0) { error = "buffer descriptor is invalid"; return {}; }
  if (!metadata_valid(attachment, error)) return {};

  struct stat status {};
  if (::fstat(fd, &status) != 0) { error = std::string("fstat failed: ") + std::strerror(errno); return {}; }
  if (!S_ISREG(status.st_mode) || status.st_size < 0 ||
      attachment.storage_size > static_cast<std::uint64_t>(status.st_size)) {
    error = "buffer descriptor is not a sufficiently large regular object";
    return {};
  }
  const int seals = ::fcntl(fd, F_GET_SEALS);
  if (seals < 0 || (seals & (F_SEAL_SHRINK | F_SEAL_GROW)) !=
                       (F_SEAL_SHRINK | F_SEAL_GROW)) {
    error = "buffer descriptor lacks required shrink and grow seals";
    return {};
  }
  const auto size = static_cast<std::size_t>(attachment.storage_size);
  void* mapping = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) { error = std::string("mmap failed: ") + std::strerror(errno); return {}; }
  return std::unique_ptr<BufferMapping>(new BufferMapping(attachment, mapping, size));
}

BufferMapping::BufferMapping(const gwipc_buffer_attach& attachment, void* mapping,
                             std::size_t mapping_size) noexcept
    : buffer_id_(attachment.buffer_id), width_(attachment.width),
      height_(attachment.height), stride_(attachment.stride),
      pixel_format_(attachment.pixel_format), mapping_(mapping),
      mapping_size_(mapping_size) {}

BufferMapping::~BufferMapping() {
  if (mapping_ != nullptr) (void)::munmap(mapping_, mapping_size_);
}

} // namespace gw::compositor
