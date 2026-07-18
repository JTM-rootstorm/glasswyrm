#include "ipc/connection_internal.hpp"
#include "ipc/wire/compositor_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include <span>

namespace {

int pixel_buffer() {
  const int fd = ::memfd_create("gwipc-sync-validation",
                                MFD_CLOEXEC | MFD_ALLOW_SEALING);
  gw::test::require(fd >= 0 && ::ftruncate(fd, 16) == 0,
                    "create validation pixel buffer");
  return fd;
}

gw::ipc::wire::BufferAttach attachment(
    const gw::ipc::wire::SynchronizationMode synchronization) {
  gw::ipc::wire::BufferAttach value;
  value.buffer_id = 1;
  value.surface_id = 2;
  value.width = value.height = 2;
  value.stride = 8;
  value.storage_size = 16;
  value.synchronization = synchronization;
  return value;
}

gwipc_status validate(const gw::ipc::wire::BufferAttach& attachment,
                      const std::span<const int> fds,
                      const bool synchronization_capability) {
  gwipc_connection connection;
  connection.config.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  connection.peer.role = GWIPC_ROLE_COMPOSITOR;
  connection.peer.capabilities =
      GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
      (synchronization_capability ? GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION : 0);
  gw::ipc::SnapshotState snapshot{true, 1, 1, UINT32_MAX, 0};
  const auto payload = gw::ipc::wire::encode(attachment);
  return gw::ipc::validate_application(
      connection, GWIPC_MESSAGE_BUFFER_ATTACH, GWIPC_FLAG_SNAPSHOT_ITEM,
      payload, fds, snapshot, gw::ipc::MessageDirection::Outgoing);
}

}  // namespace

int main() {
  const int pixel = pixel_buffer();
  const int readiness = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  gw::test::require(readiness >= 0, "create validation eventfd");
  const int synchronized_fds[]{pixel, readiness};
  gw::test::require(
      validate(attachment(gw::ipc::wire::SynchronizationMode::EventFd),
               synchronized_fds, true) == GWIPC_STATUS_OK,
      "eventfd synchronization accepts pixel and readiness descriptors");
  gw::test::require(
      validate(attachment(gw::ipc::wire::SynchronizationMode::EventFd),
               std::span(synchronized_fds).first(1), true) ==
          GWIPC_STATUS_PROTOCOL_ERROR,
      "eventfd synchronization requires exactly two descriptors");
  gw::test::require(
      validate(attachment(gw::ipc::wire::SynchronizationMode::EventFd),
               synchronized_fds, false) == GWIPC_STATUS_CAPABILITY_MISMATCH,
      "eventfd synchronization requires its negotiated capability");
  gw::test::require(
      validate(attachment(gw::ipc::wire::SynchronizationMode::None),
               std::span(synchronized_fds).first(1), false) ==
          GWIPC_STATUS_OK,
      "historical synchronization-none attachment keeps one descriptor");

  const int blocking = ::eventfd(0, EFD_CLOEXEC);
  const int blocking_fds[]{pixel, blocking};
  gw::test::require(
      blocking >= 0 &&
          validate(attachment(gw::ipc::wire::SynchronizationMode::EventFd),
                   blocking_fds, true) == GWIPC_STATUS_PROTOCOL_ERROR,
      "blocking eventfds are rejected");
  (void)::close(blocking);
  (void)::close(readiness);
  (void)::close(pixel);
  return 0;
}
