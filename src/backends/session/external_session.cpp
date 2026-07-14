#include "backends/session/external_session.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace glasswyrm::session {

int PosixFileDescriptorApi::duplicate_close_on_exec(const int fd) {
  const int duplicate = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) error_number_ = errno;
  return duplicate;
}

void PosixFileDescriptorApi::close_fd(const int fd) noexcept {
  if (fd >= 0) (void)::close(fd);
}

std::string PosixFileDescriptorApi::last_error() const {
  return error_number_ == 0 ? std::string{} : std::strerror(error_number_);
}

ExternalDeviceSession::~ExternalDeviceSession() { reset(); }

bool ExternalDeviceSession::adopt(const int caller_fd, std::string& error) {
  error.clear();
  if (device_fd_ >= 0) {
    error = "external device session already owns a descriptor";
    return false;
  }
  if (caller_fd < 0) {
    error = "external session requires a valid caller-owned descriptor";
    return false;
  }
  device_fd_ = api_.duplicate_close_on_exec(caller_fd);
  if (device_fd_ < 0) {
    error = "duplicate external DRM descriptor";
    const auto cause = api_.last_error();
    if (!cause.empty()) {
      error += ": ";
      error += cause;
    }
    return false;
  }
  return true;
}

void ExternalDeviceSession::reset() noexcept {
  if (device_fd_ < 0) return;
  api_.close_fd(device_fd_);
  device_fd_ = -1;
}

} // namespace glasswyrm::session
