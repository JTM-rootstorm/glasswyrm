#ifndef GLASSWYRM_IPC_HPP
#define GLASSWYRM_IPC_HPP

#include <glasswyrm/ipc.h>

#include <utility>
#include <unistd.h>

namespace glasswyrm::ipc {

class OwnedFd {
 public:
  OwnedFd() noexcept = default;
  explicit OwnedFd(int fd) noexcept : fd_(fd) {}
  ~OwnedFd() noexcept { reset(); }
  OwnedFd(const OwnedFd&) = delete;
  OwnedFd& operator=(const OwnedFd&) = delete;
  OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  OwnedFd& operator=(OwnedFd&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }
  int release() noexcept { return std::exchange(fd_, -1); }
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) (void)::close(fd_);
    fd_ = fd;
  }

 private:
  int fd_{-1};
};

class Message {
 public:
  Message() noexcept = default;
  explicit Message(gwipc_message* message) noexcept : message_(message) {}
  ~Message() noexcept { gwipc_message_destroy(message_); }
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
  Message(Message&& other) noexcept
      : message_(std::exchange(other.message_, nullptr)) {}
  Message& operator=(Message&& other) noexcept {
    if (this != &other) {
      gwipc_message_destroy(message_);
      message_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] gwipc_message* get() const noexcept { return message_; }
  [[nodiscard]] explicit operator bool() const noexcept { return message_; }
  gwipc_message* release() noexcept {
    return std::exchange(message_, nullptr);
  }
  gwipc_status take_fd(size_t index, OwnedFd& out) noexcept {
    int fd = -1;
    const auto status = gwipc_message_take_fd(message_, index, &fd);
    if (status == GWIPC_STATUS_OK) out.reset(fd);
    return status;
  }

 private:
  gwipc_message* message_{nullptr};
};

class Connection {
 public:
  Connection() noexcept = default;
  explicit Connection(gwipc_connection* connection) noexcept
      : connection_(connection) {}
  ~Connection() noexcept { gwipc_connection_destroy(connection_); }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&& other) noexcept
      : connection_(std::exchange(other.connection_, nullptr)) {}
  Connection& operator=(Connection&& other) noexcept {
    if (this != &other) {
      gwipc_connection_destroy(connection_);
      connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
  }

  static gwipc_status connect(const gwipc_connection_options& options,
                              Connection& out) noexcept {
    gwipc_connection* connection = nullptr;
    const auto status = gwipc_connection_connect(&options, &connection);
    if (status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS)
      out = Connection(connection);
    return status;
  }
  [[nodiscard]] gwipc_connection* get() const noexcept { return connection_; }
  [[nodiscard]] explicit operator bool() const noexcept { return connection_; }
  gwipc_status receive(Message& out) noexcept {
    gwipc_message* message = nullptr;
    const auto status = gwipc_connection_receive(connection_, &message);
    if (status == GWIPC_STATUS_OK) out = Message(message);
    return status;
  }

 private:
  gwipc_connection* connection_{nullptr};
};

class Listener {
 public:
  Listener() noexcept = default;
  explicit Listener(gwipc_listener* listener) noexcept : listener_(listener) {}
  ~Listener() noexcept { gwipc_listener_destroy(listener_); }
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;
  Listener(Listener&& other) noexcept
      : listener_(std::exchange(other.listener_, nullptr)) {}
  Listener& operator=(Listener&& other) noexcept {
    if (this != &other) {
      gwipc_listener_destroy(listener_);
      listener_ = std::exchange(other.listener_, nullptr);
    }
    return *this;
  }

  static gwipc_status create(const gwipc_listener_options& options,
                             Listener& out) noexcept {
    gwipc_listener* listener = nullptr;
    const auto status = gwipc_listener_create(&options, &listener);
    if (status == GWIPC_STATUS_OK) out = Listener(listener);
    return status;
  }
  [[nodiscard]] gwipc_listener* get() const noexcept { return listener_; }
  [[nodiscard]] explicit operator bool() const noexcept { return listener_; }
  gwipc_status accept(Connection& out) noexcept {
    gwipc_connection* connection = nullptr;
    const auto status = gwipc_listener_accept(listener_, &connection);
    if (status == GWIPC_STATUS_OK) out = Connection(connection);
    return status;
  }

 private:
  gwipc_listener* listener_{nullptr};
};

}  // namespace glasswyrm::ipc

#endif
