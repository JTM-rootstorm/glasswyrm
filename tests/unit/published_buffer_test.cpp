#include "glasswyrmd/published_buffer.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
  using namespace glasswyrm::server;
  auto pixels = PixelStorage::create(4, 3);
  if (!pixels) return 1;
  pixels->at(2, 1) = 0xff123456U;
  auto published = PublishedWindowBuffer::create(1, 0x200001, *pixels);
  if (!published || published->size() != 48 ||
      (::fcntl(published->fd(), F_GETFD) & FD_CLOEXEC) == 0) return 2;
  const auto seals = ::fcntl(published->fd(), F_GET_SEALS);
  if ((seals & (F_SEAL_SHRINK | F_SEAL_GROW)) !=
      (F_SEAL_SHRINK | F_SEAL_GROW)) return 3;
  void* mapping = ::mmap(nullptr, published->size(), PROT_READ, MAP_SHARED,
                         published->fd(), 0);
  if (mapping == MAP_FAILED) return 4;
  const auto* words = static_cast<const std::uint32_t*>(mapping);
  const bool copied = words[6] == 0xff123456U;
  (void)::munmap(mapping, published->size());
  if (!copied) return 5;

  auto synchronized = PublishedWindowBuffer::create(
      2, 0x200002, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  if (!synchronized || synchronized->synchronization_fd() < 0 ||
      (::fcntl(synchronized->synchronization_fd(), F_GETFD) & FD_CLOEXEC) ==
          0 ||
      (::fcntl(synchronized->synchronization_fd(), F_GETFL) & O_NONBLOCK) ==
          0 ||
      !synchronized->signal_ready())
    return 7;
  std::uint64_t token = 0;
  if (::read(synchronized->synchronization_fd(), &token, sizeof(token)) !=
          static_cast<ssize_t>(sizeof(token)) ||
      token != 1 || !synchronized->signal_ready() ||
      !synchronized->retract_ready())
    return 8;

  PublishedBufferStore store(96);
  if (!store.install(0x200001, std::move(published)) ||
      store.accounted_bytes() != 48 || store.next_buffer_id() != 1 ||
      !store.retire(0x200001, PublishedBufferRetirement::Replaced) ||
      store.release(1, PublishedBufferRetirement::SurfaceRemoved) ||
      !store.release(1, PublishedBufferRetirement::Replaced) ||
      store.accounted_bytes() != 0) return 6;
  return 0;
}
