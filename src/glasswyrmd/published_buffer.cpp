#include "glasswyrmd/published_buffer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

int create_memfd() noexcept {
#ifdef SYS_memfd_create
  return static_cast<int>(::syscall(SYS_memfd_create, "glasswyrm-window",
                                    MFD_CLOEXEC | MFD_ALLOW_SEALING));
#else
  errno = ENOSYS;
  return -1;
#endif
}

}  // namespace

PublishedWindowBuffer::PublishedWindowBuffer(
    const std::uint64_t buffer_id, const std::uint32_t window_xid,
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t stride, const std::size_t size, const int fd,
    void* const mapping) noexcept
    : buffer_id_(buffer_id),
      window_xid_(window_xid),
      width_(width),
      height_(height),
      stride_(stride),
      size_(size),
      fd_(fd),
      mapping_(mapping) {}

PublishedWindowBuffer::~PublishedWindowBuffer() {
  if (mapping_ != nullptr) (void)::munmap(mapping_, size_);
  if (fd_ >= 0) (void)::close(fd_);
}

std::unique_ptr<PublishedWindowBuffer> PublishedWindowBuffer::create(
    const std::uint64_t buffer_id, const std::uint32_t window_xid,
    const PixelStorage& canonical) {
  if (buffer_id == 0 || window_xid == 0 || canonical.byte_size() == 0 ||
      canonical.byte_size() > static_cast<std::size_t>(
                                  std::numeric_limits<off_t>::max()))
    return nullptr;
  const int fd = create_memfd();
  if (fd < 0) return nullptr;
  const auto size = canonical.byte_size();
  if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
    (void)::close(fd);
    return nullptr;
  }
  void* const mapping =
      ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    (void)::close(fd);
    return nullptr;
  }
  if (::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0) {
    (void)::munmap(mapping, size);
    (void)::close(fd);
    return nullptr;
  }
  auto result = std::unique_ptr<PublishedWindowBuffer>(
      new PublishedWindowBuffer(buffer_id, window_xid, canonical.width(),
                                canonical.height(), canonical.stride(), size,
                                fd, mapping));
  if (!result->copy_all_from(canonical)) return nullptr;
  return result;
}

bool PublishedWindowBuffer::copy_from(
    const PixelStorage& canonical,
    const std::span<const geometry::Rectangle> rectangles) noexcept {
  if (canonical.width() != width_ || canonical.height() != height_ ||
      canonical.stride() != stride_ || mapping_ == nullptr)
    return false;
  auto* const destination = static_cast<std::byte*>(mapping_);
  const auto source = canonical.pixels();
  for (const auto rectangle : rectangles) {
    const auto clipped = geometry::intersect(
        rectangle, {0, 0, canonical.width(), canonical.height()});
    if (!clipped) continue;
    for (std::uint32_t row = 0; row < clipped->height; ++row) {
      const auto y = static_cast<std::uint32_t>(clipped->y) + row;
      const auto x = static_cast<std::uint32_t>(clipped->x);
      const auto offset = static_cast<std::size_t>(y) * stride_ +
                          static_cast<std::size_t>(x) * 4U;
      std::memcpy(destination + offset,
                  source.data() + static_cast<std::size_t>(y) * width_ + x,
                  static_cast<std::size_t>(clipped->width) * 4U);
    }
  }
  return ::msync(mapping_, size_, MS_SYNC) == 0;
}

bool PublishedWindowBuffer::copy_all_from(
    const PixelStorage& canonical) noexcept {
  const geometry::Rectangle bounds{0, 0, width_, height_};
  return copy_from(canonical, std::span<const geometry::Rectangle>(&bounds, 1));
}

std::uint64_t PublishedBufferStore::next_buffer_id() noexcept {
  if (next_id_ == 0) return 0;
  const auto result = next_id_;
  if (next_id_ == std::numeric_limits<std::uint64_t>::max()) next_id_ = 0;
  else ++next_id_;
  return result;
}

bool PublishedBufferStore::install(
    const std::uint32_t window_xid,
    std::unique_ptr<PublishedWindowBuffer> buffer) {
  if (!buffer || window_xid == 0 || buffer->window_xid() != window_xid ||
      current_.contains(window_xid) ||
      buffer->size() > maximum_bytes_ - accounted_bytes_)
    return false;
  accounted_bytes_ += buffer->size();
  current_.emplace(window_xid, std::move(buffer));
  return true;
}

PublishedWindowBuffer* PublishedBufferStore::current(
    const std::uint32_t window_xid) noexcept {
  const auto found = current_.find(window_xid);
  return found == current_.end() ? nullptr : found->second.get();
}

const PublishedWindowBuffer* PublishedBufferStore::current(
    const std::uint32_t window_xid) const noexcept {
  const auto found = current_.find(window_xid);
  return found == current_.end() ? nullptr : found->second.get();
}

bool PublishedBufferStore::retire(const std::uint32_t window_xid,
                                  const PublishedBufferRetirement reason) {
  const auto found = current_.find(window_xid);
  if (found == current_.end()) return false;
  const auto buffer_id = found->second->buffer_id();
  if (retired_.contains(buffer_id)) return false;
  retired_.emplace(buffer_id, Retired{reason, std::move(found->second)});
  current_.erase(found);
  return true;
}

bool PublishedBufferStore::release(const std::uint64_t buffer_id,
                                   const PublishedBufferRetirement reason) {
  const auto found = retired_.find(buffer_id);
  if (found == retired_.end() || found->second.reason != reason) return false;
  accounted_bytes_ -= found->second.buffer->size();
  retired_.erase(found);
  return true;
}

void PublishedBufferStore::peer_disconnected() noexcept {
  for (auto& [window, buffer] : current_) {
    (void)window;
    buffer->set_announced(false);
  }
  for (const auto& [id, retired] : retired_) {
    (void)id;
    accounted_bytes_ -= retired.buffer->size();
  }
  retired_.clear();
}

}  // namespace glasswyrm::server
