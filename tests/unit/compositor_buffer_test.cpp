#include "compositor/buffer.hpp"
#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <linux/memfd.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace {
gwipc_buffer_attach attachment() {
  gwipc_buffer_attach value{};
  value.struct_size = sizeof(value);
  value.buffer_id = 7;
  value.surface_id = 3;
  value.width = 2;
  value.height = 2;
  value.stride = 8;
  value.storage_size = 16;
  value.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  value.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  value.synchronization = GWIPC_SYNCHRONIZATION_NONE;
  return value;
}

int memfd(bool seal = true, std::size_t size = 16) {
  const int fd = ::memfd_create("gwcomp-buffer-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  gw::test::require(fd >= 0 && ::ftruncate(fd, static_cast<off_t>(size)) == 0,
                    "create test memfd");
  if (seal) gw::test::require(::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
                             "seal test memfd");
  return fd;
}

void rejected_fd_is_closed(gwipc_buffer_attach value, int fd) {
  std::string error;
  gw::test::require(!gw::compositor::BufferMapping::import(value, fd, error), "import rejects invalid buffer");
  gw::test::require(::fcntl(fd, F_GETFD) == -1, "rejected descriptor is closed");
}
}

int main() {
  std::string error;
  const int fd = memfd();
  auto mapping = gw::compositor::BufferMapping::import(attachment(), fd, error);
  gw::test::require(mapping != nullptr && mapping->bytes().size() == 16, "sealed buffer imports");
  gw::test::require(::fcntl(fd, F_GETFD) == -1, "successful import closes descriptor");

  rejected_fd_is_closed(attachment(), memfd(false));
  auto invalid = attachment(); invalid.byte_offset = 4;
  rejected_fd_is_closed(invalid, memfd());
  invalid = attachment(); invalid.stride = 4;
  rejected_fd_is_closed(invalid, memfd());
  invalid = attachment(); invalid.storage_size = 15;
  rejected_fd_is_closed(invalid, memfd());
  invalid = attachment(); invalid.pixel_format = GWIPC_PIXEL_FORMAT_ARGB8888;
  rejected_fd_is_closed(invalid, memfd());
  invalid = attachment(); invalid.flags = 1;
  rejected_fd_is_closed(invalid, memfd());
  invalid = attachment(); invalid.storage_size = gw::compositor::BufferMapping::kMaximumMappingBytes + 1;
  rejected_fd_is_closed(invalid, memfd());
  return 0;
}
