#include "glasswyrmd/server.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

bool make_address(const std::string &path, sockaddr_un &address,
                  socklen_t &length) {
  if (path.size() >= sizeof(address.sun_path)) {
    std::fprintf(stderr, "glasswyrmd: socket path is too long: %s\n",
                 path.c_str());
    return false;
  }
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  length =
      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
  return true;
}

} // namespace

bool Server::remove_stale_socket() {
  struct stat before{};
  if (::lstat(socket_path_.c_str(), &before) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    std::fprintf(stderr, "glasswyrmd: cannot inspect %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    return false;
  }
  if (!S_ISSOCK(before.st_mode)) {
    std::fprintf(stderr, "glasswyrmd: refusing to replace non-socket path %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (before.st_uid != ::geteuid()) {
    std::fprintf(stderr,
                 "glasswyrmd: refusing to remove socket owned by another "
                 "user: %s\n",
                 socket_path_.c_str());
    return false;
  }

  sockaddr_un address{};
  socklen_t address_length = 0;
  if (!make_address(socket_path_, address, address_length)) {
    return false;
  }
  const int probe =
      ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (probe < 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create socket probe: %s\n",
                 std::strerror(errno));
    return false;
  }
  const int result =
      ::connect(probe, reinterpret_cast<sockaddr *>(&address), address_length);
  const int connect_error = errno;
  ::close(probe);
  if (result == 0 ||
      (connect_error != ECONNREFUSED && connect_error != ENOENT)) {
    std::fprintf(stderr, "glasswyrmd: display socket is already active: %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (connect_error == ENOENT) {
    return true;
  }

  struct stat after{};
  if (::lstat(socket_path_.c_str(), &after) != 0 ||
      before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      !S_ISSOCK(after.st_mode)) {
    std::fprintf(stderr,
                 "glasswyrmd: socket changed while checking staleness: %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (::unlink(socket_path_.c_str()) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot remove stale socket %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    return false;
  }
  return true;
}

bool Server::prepare_socket_path() {
  std::error_code error;
  std::filesystem::create_directories(options_.socket_dir, error);
  if (error) {
    std::fprintf(stderr, "glasswyrmd: cannot create socket directory %s: %s\n",
                 options_.socket_dir.c_str(), error.message().c_str());
    return false;
  }
  const auto status =
      std::filesystem::symlink_status(options_.socket_dir, error);
  if (error || !std::filesystem::is_directory(status)) {
    std::fprintf(stderr,
                 "glasswyrmd: socket directory is not a directory: %s\n",
                 options_.socket_dir.c_str());
    return false;
  }
  return remove_stale_socket();
}

bool Server::open_listener() {
  if (!prepare_socket_path()) {
    return false;
  }
  sockaddr_un address{};
  socklen_t address_length = 0;
  if (!make_address(socket_path_, address, address_length)) {
    return false;
  }
  listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listener_ < 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create listener: %s\n",
                 std::strerror(errno));
    return false;
  }
  if (::bind(listener_, reinterpret_cast<sockaddr *>(&address),
             address_length) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot bind %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    close_listener();
    return false;
  }
  struct stat status{};
  if (::lstat(socket_path_.c_str(), &status) != 0 ||
      !S_ISSOCK(status.st_mode)) {
    std::fprintf(stderr, "glasswyrmd: cannot record bound socket identity\n");
    close_listener();
    return false;
  }
  socket_device_ = status.st_dev;
  socket_inode_ = status.st_ino;
  if (::listen(listener_, 32) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot listen on %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    close_listener();
    unlink_owned_socket();
    return false;
  }
  return true;
}

void Server::close_listener() {
  if (listener_ >= 0) {
    ::close(listener_);
    listener_ = -1;
  }
}

void Server::unlink_owned_socket() {
  if (socket_inode_ == 0) {
    return;
  }
  struct stat status{};
  if (::lstat(socket_path_.c_str(), &status) == 0 &&
      status.st_dev == socket_device_ && status.st_ino == socket_inode_ &&
      S_ISSOCK(status.st_mode)) {
    if (::unlink(socket_path_.c_str()) != 0) {
      std::fprintf(stderr, "glasswyrmd: cannot remove socket %s: %s\n",
                   socket_path_.c_str(), std::strerror(errno));
    }
  }
  socket_device_ = 0;
  socket_inode_ = 0;
}

} // namespace glasswyrm::server
