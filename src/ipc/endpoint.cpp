#include "ipc/endpoint.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

namespace gw::ipc {
namespace {

bool make_address(const std::string& path, sockaddr_un& address,
                  socklen_t& length) noexcept {
  if (path.empty() || path.size() >= sizeof(address.sun_path)) return false;
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                  path.size() + 1);
  return true;
}

std::string parent_path(const std::string& path) {
  const auto separator = path.find_last_of('/');
  if (separator == std::string::npos) return ".";
  if (separator == 0) return "/";
  return path.substr(0, separator);
}

bool is_live_socket(const std::string& path) noexcept {
  sockaddr_un address{};
  socklen_t length = 0;
  if (!make_address(path, address, length)) return true;
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          0);
  if (fd < 0) return true;
  const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), length);
  const int error = errno;
  (void)::close(fd);
  return result == 0 || error == EINPROGRESS || error == EAGAIN ||
         error == EALREADY;
}

}  // namespace

gwipc_status prepare_endpoint_path(const std::string& path,
                                   int& system_errno) noexcept {
  sockaddr_un address{};
  socklen_t length = 0;
  if (!make_address(path, address, length)) return GWIPC_STATUS_INVALID_ARGUMENT;

  struct stat parent_status {};
  if (::lstat(parent_path(path).c_str(), &parent_status) < 0) {
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (!S_ISDIR(parent_status.st_mode)) return GWIPC_STATUS_INVALID_ARGUMENT;

  struct stat status {};
  if (::lstat(path.c_str(), &status) < 0) {
    if (errno == ENOENT) return GWIPC_STATUS_OK;
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (!S_ISSOCK(status.st_mode) || status.st_uid != ::geteuid())
    return GWIPC_STATUS_INVALID_ARGUMENT;
  if (is_live_socket(path)) return GWIPC_STATUS_INVALID_STATE;

  struct stat unchanged {};
  if (::lstat(path.c_str(), &unchanged) < 0 ||
      unchanged.st_dev != status.st_dev || unchanged.st_ino != status.st_ino) {
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (::unlink(path.c_str()) < 0) {
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status bind_endpoint(const std::string& path, int& out_fd,
                           EndpointIdentity& identity,
                           int& system_errno) noexcept {
  const auto prepared = prepare_endpoint_path(path, system_errno);
  if (prepared != GWIPC_STATUS_OK) return prepared;

  sockaddr_un address{};
  socklen_t length = 0;
  (void)make_address(path, address, length);
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          0);
  if (fd < 0) {
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), length) < 0) {
    system_errno = errno;
    (void)::close(fd);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  struct stat status {};
  if (::lstat(path.c_str(), &status) < 0 || !S_ISSOCK(status.st_mode) ||
      status.st_uid != ::geteuid()) {
    system_errno = errno;
    (void)::close(fd);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  identity = {status.st_dev, status.st_ino};
  if (::chmod(path.c_str(), 0600) < 0 || ::listen(fd, 32) < 0) {
    system_errno = errno;
    (void)::close(fd);
    cleanup_endpoint(path, identity);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  struct stat verified {};
  if (::lstat(path.c_str(), &verified) < 0 ||
      verified.st_dev != identity.device || verified.st_ino != identity.inode ||
      !S_ISSOCK(verified.st_mode)) {
    system_errno = ESTALE;
    (void)::close(fd);
    cleanup_endpoint(path, identity);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  out_fd = fd;
  return GWIPC_STATUS_OK;
}

void cleanup_endpoint(const std::string& path,
                      const EndpointIdentity& identity) noexcept {
  struct stat status {};
  if (::lstat(path.c_str(), &status) == 0 && S_ISSOCK(status.st_mode) &&
      status.st_dev == identity.device && status.st_ino == identity.inode)
    (void)::unlink(path.c_str());
}

gwipc_status connect_endpoint(const std::string& path, int& out_fd,
                              bool& in_progress,
                              int& system_errno) noexcept {
  sockaddr_un address{};
  socklen_t length = 0;
  if (!make_address(path, address, length)) return GWIPC_STATUS_INVALID_ARGUMENT;
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          0);
  if (fd < 0) {
    system_errno = errno;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), length) == 0) {
    out_fd = fd;
    in_progress = false;
    return GWIPC_STATUS_OK;
  }
  if (errno == EINPROGRESS || errno == EAGAIN) {
    out_fd = fd;
    in_progress = true;
    return GWIPC_STATUS_IN_PROGRESS;
  }
  system_errno = errno;
  (void)::close(fd);
  return GWIPC_STATUS_SYSTEM_ERROR;
}

bool read_peer_credentials(int fd, gwipc_peer_info& peer,
                           int& system_errno) noexcept {
  struct ucred credentials {};
  socklen_t size = sizeof(credentials);
  if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0 ||
      size != sizeof(credentials)) {
    system_errno = errno;
    return false;
  }
  peer.pid = credentials.pid;
  peer.uid = credentials.uid;
  peer.gid = credentials.gid;
  return true;
}

}  // namespace gw::ipc
