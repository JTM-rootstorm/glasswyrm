#include "ipc/internal.hpp"

#include <unistd.h>

#include <new>
#include <utility>

namespace gw::ipc {

void close_fd(int& fd) noexcept {
  if (fd >= 0) {
    (void)::close(fd);
    fd = -1;
  }
}

QueuedRecord::QueuedRecord(QueuedRecord&& other) noexcept
    : bytes(std::move(other.bytes)),
      fds(std::move(other.fds)),
      sequence(other.sequence) {
  other.fds.clear();
}

QueuedRecord& QueuedRecord::operator=(QueuedRecord&& other) noexcept {
  if (this != &other) {
    for (auto& fd : fds) close_fd(fd);
    bytes = std::move(other.bytes);
    fds = std::move(other.fds);
    sequence = other.sequence;
    other.fds.clear();
  }
  return *this;
}

QueuedRecord::~QueuedRecord() {
  for (auto& fd : fds) close_fd(fd);
}

}  // namespace gw::ipc

gwipc_message::~gwipc_message() {
  for (auto& fd : fds) gw::ipc::close_fd(fd);
}

gwipc_connection::~gwipc_connection() {
  gw::ipc::close_fd(fd);
  for (auto* message : incoming) delete message;
}

extern "C" {

uint16_t gwipc_message_type(const gwipc_message* message) {
  return message ? message->type : 0;
}

uint32_t gwipc_message_flags(const gwipc_message* message) {
  return message ? message->flags : 0;
}

uint64_t gwipc_message_sequence(const gwipc_message* message) {
  return message ? message->sequence : 0;
}

uint64_t gwipc_message_reply_to(const gwipc_message* message) {
  return message ? message->reply_to : 0;
}

const uint8_t* gwipc_message_payload(const gwipc_message* message,
                                     size_t* out_size) {
  if (out_size) *out_size = message ? message->payload.size() : 0;
  if (!message || message->payload.empty()) return nullptr;
  return message->payload.data();
}

size_t gwipc_message_fd_count(const gwipc_message* message) {
  if (!message) return 0;
  size_t count = 0;
  for (const int fd : message->fds) count += fd >= 0 ? 1U : 0U;
  return count;
}

gwipc_status gwipc_message_take_fd(gwipc_message* message, size_t index,
                                   int* out_fd) {
  if (!message || !out_fd || index >= message->fds.size())
    return GWIPC_STATUS_INVALID_ARGUMENT;
  if (message->fds[index] < 0) return GWIPC_STATUS_INVALID_STATE;
  *out_fd = std::exchange(message->fds[index], -1);
  return GWIPC_STATUS_OK;
}

void gwipc_message_destroy(gwipc_message* message) { delete message; }

}  // extern "C"
