#include "glasswyrmd/server.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/runtime_bridge.hpp"
#endif

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t signal_write_descriptor = -1;

void request_stop(int) {
  const int saved_errno = errno;
  stop_requested = 1;
  if (signal_write_descriptor >= 0) {
    const std::uint8_t byte = 1;
    (void)::write(static_cast<int>(signal_write_descriptor), &byte,
                  sizeof(byte));
  }
  errno = saved_errno;
}

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

Server::Server(Options options) : options_(std::move(options)) {
  socket_path_ = options_.socket_dir + "/X" +
                 std::to_string(static_cast<unsigned int>(options_.display));
}

Server::~Server() {
  clients_.clear();
  close_listener();
  unlink_owned_socket();
}

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

std::optional<std::uint32_t> Server::allocate_resource_base() const {
  constexpr std::uint64_t first_base = 0x00200000U;
  constexpr std::uint64_t last_base = 0xffe00000U;
  constexpr std::uint64_t stride = 0x00200000U;
  for (std::uint64_t candidate = first_base; candidate <= last_base;
       candidate += stride) {
    const auto base = static_cast<std::uint32_t>(candidate);
    const bool in_use = std::any_of(clients_.begin(), clients_.end(),
                                    [base](const auto &client) {
                                      return client->resource_id_base() == base;
                                    });
    if (!in_use) {
      return base;
    }
  }
  return std::nullopt;
}

void Server::accept_clients() {
  for (;;) {
    const int descriptor =
        ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (descriptor >= 0) {
      const auto resource_base = allocate_resource_base();
      if (!resource_base) {
        std::fprintf(stderr,
                     "glasswyrmd: client resource-ID space exhausted\n");
        ::close(descriptor);
        continue;
      }
      clients_.push_back(std::make_unique<ClientConnection>(
          descriptor, next_client_identifier_++, *resource_base, state_));
      std::fprintf(
          stderr, "glasswyrmd: accepted client %llu\n",
          static_cast<unsigned long long>(next_client_identifier_ - 1));
      continue;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    std::fprintf(stderr, "glasswyrmd: accept failed: %s\n",
                 std::strerror(errno));
    return;
  }
}

void Server::remove_closed_clients() {
  std::erase_if(clients_, [](const auto &client) {
    return client->state() == ClientConnection::State::Closing;
  });
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

int Server::run() {
  stop_requested = 0;
  int signal_pipe[2] = {-1, -1};
  if (::pipe2(signal_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create signal wakeup pipe: %s\n",
                 std::strerror(errno));
    return 1;
  }
  signal_write_descriptor = signal_pipe[1];
  const auto close_signal_pipe = [&signal_pipe] {
    signal_write_descriptor = -1;
    ::close(signal_pipe[0]);
    ::close(signal_pipe[1]);
    signal_pipe[0] = -1;
    signal_pipe[1] = -1;
  };
  struct sigaction action{};
  action.sa_handler = request_stop;
  ::sigemptyset(&action.sa_mask);
  if (::sigaction(SIGINT, &action, nullptr) != 0 ||
      ::sigaction(SIGTERM, &action, nullptr) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot install signal handlers: %s\n",
                 std::strerror(errno));
    close_signal_pipe();
    return 1;
  }
  struct sigaction ignore_pipe{};
  ignore_pipe.sa_handler = SIG_IGN;
  ::sigemptyset(&ignore_pipe.sa_mask);
  (void)::sigaction(SIGPIPE, &ignore_pipe, nullptr);

#ifdef GW_SERVER_HAS_IPC
  std::unique_ptr<RuntimeBridge> bridge;
  if (options_.integrated()) {
    bridge = std::make_unique<RuntimeBridge>(
        *options_.wm_socket, *options_.compositor_socket, state_.screen());
    bridge->start();
    while (!stop_requested && !bridge->ready()) {
      pollfd bootstrap[3] = {
          {signal_pipe[0], POLLIN, 0},
          {bridge->policy_fd(), bridge->policy_events(), 0},
          {bridge->compositor_fd(), bridge->compositor_events(), 0}};
      const int count = ::poll(
          bootstrap, 3, bridge->poll_timeout_ms(RuntimeBridge::Clock::now()));
      if (count < 0 && errno == EINTR)
        continue;
      if (count < 0) {
        std::fprintf(stderr,
                     "glasswyrmd: integrated bootstrap poll failed: %s\n",
                     std::strerror(errno));
        close_signal_pipe();
        return 1;
      }
      if ((bootstrap[0].revents & POLLIN) != 0) {
        stop_requested = 1;
        break;
      }
      std::string error;
      if (!bridge->service(bootstrap[1].revents, bootstrap[2].revents,
                           RuntimeBridge::Clock::now(), error)) {
        std::fprintf(stderr, "glasswyrmd: integrated bootstrap failed: %s\n",
                     error.c_str());
        close_signal_pipe();
        return 1;
      }
    }
    if (stop_requested) {
      close_signal_pipe();
      return 0;
    }
  }
#else
  if (options_.integrated()) {
    std::fprintf(stderr,
                 "glasswyrmd: integrated mode requires libgwipc support\n");
    close_signal_pipe();
    return 1;
  }
#endif

  if (!open_listener()) {
    close_signal_pipe();
    return 1;
  }
  std::fprintf(stderr, "glasswyrmd: listening on %s\n", socket_path_.c_str());

  while (!stop_requested) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(clients_.size() + 4);
    short listener_events = POLLIN;
#ifdef GW_SERVER_HAS_IPC
    if (bridge && !bridge->ready())
      listener_events = 0;
#endif
    descriptors.push_back(pollfd{listener_, listener_events, 0});
    descriptors.push_back(pollfd{signal_pipe[0], POLLIN, 0});
#ifdef GW_SERVER_HAS_IPC
    if (bridge) {
      descriptors.push_back(
          pollfd{bridge->policy_fd(), bridge->policy_events(), 0});
      descriptors.push_back(
          pollfd{bridge->compositor_fd(), bridge->compositor_events(), 0});
    }
#endif
    const std::size_t client_offset = descriptors.size();
    for (const auto &client : clients_) {
      descriptors.push_back(
          pollfd{client->descriptor(), client->poll_events(), 0});
    }
    const bool pending_work =
        std::any_of(clients_.begin(), clients_.end(),
                    [](const auto &client) { return client->needs_service(); });
    int poll_timeout = pending_work ? 0 : -1;
#ifdef GW_SERVER_HAS_IPC
    if (bridge && !bridge->ready())
      poll_timeout = bridge->poll_timeout_ms(RuntimeBridge::Clock::now());
#endif
    const int result =
        ::poll(descriptors.data(), descriptors.size(), poll_timeout);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::fprintf(stderr, "glasswyrmd: poll failed: %s\n",
                   std::strerror(errno));
      close_signal_pipe();
      return 1;
    }
    const std::size_t polled_client_count = clients_.size();
    for (std::size_t index = 0; index < polled_client_count; ++index) {
      clients_[index]->handle_events(
          descriptors[index + client_offset].revents);
    }
#ifdef GW_SERVER_HAS_IPC
    if (bridge) {
      std::string error;
      if (!bridge->service(descriptors[2].revents, descriptors[3].revents,
                           RuntimeBridge::Clock::now(), error)) {
        std::fprintf(stderr, "glasswyrmd: integrated runtime failed: %s\n",
                     error.c_str());
        close_signal_pipe();
        return 1;
      }
    }
#endif
    remove_closed_clients();
    bool accepts_ready = true;
#ifdef GW_SERVER_HAS_IPC
    accepts_ready = !bridge || bridge->ready();
#endif
    if (accepts_ready && (descriptors.front().revents & POLLIN) != 0) {
      accept_clients();
    }
  }

  clients_.clear();
  close_signal_pipe();
  close_listener();
  unlink_owned_socket();
  std::fprintf(stderr, "glasswyrmd: stopped\n");
  return 0;
}

} // namespace glasswyrm::server
