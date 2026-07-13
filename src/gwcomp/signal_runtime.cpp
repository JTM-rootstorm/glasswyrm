#include "gwcomp/signal_runtime.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <unistd.h>

namespace glasswyrm::compositor {
namespace {

volatile std::sig_atomic_t signal_pipe_fd = -1;

constexpr unsigned char signal_tag(const int signal) noexcept {
  if (signal == SIGUSR1) return 'R';
  if (signal == SIGUSR2) return 'A';
  return 'S';
}

void signal_handler(const int signal) noexcept {
  const int saved_errno = errno;
  const int fd = signal_pipe_fd;
  if (fd >= 0) {
    const unsigned char tag = signal_tag(signal);
    (void)::write(fd, &tag, sizeof(tag));
  }
  errno = saved_errno;
}

bool install_handler(const int signal, struct sigaction& previous) noexcept {
  struct sigaction action {};
  action.sa_handler = signal_handler;
  (void)::sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  return ::sigaction(signal, &action, &previous) == 0;
}

}  // namespace

struct SignalRuntime::SignalActionStorage {
  struct sigaction interrupt {};
  struct sigaction terminate {};
  struct sigaction release {};
  struct sigaction acquire {};
};

SignalRuntime::~SignalRuntime() { restore(); }

bool SignalRuntime::install(const bool virtual_terminal_events,
                            std::string& error) {
  error.clear();
  if (pipe_[0] >= 0 || signal_pipe_fd >= 0) {
    error = "signal runtime is already installed";
    return false;
  }
  actions_ = new (std::nothrow) SignalActionStorage;
  if (!actions_) {
    error = "cannot allocate signal action storage";
    return false;
  }
  if (::pipe2(pipe_, O_NONBLOCK | O_CLOEXEC) != 0) {
    error = std::string("cannot create signal pipe: ") + std::strerror(errno);
    restore();
    return false;
  }
  signal_pipe_fd = pipe_[1];
  if (!install_handler(SIGINT, actions_->interrupt)) {
    error = std::string("cannot install SIGINT handler: ") +
            std::strerror(errno);
    restore();
    return false;
  }
  interrupt_installed_ = true;
  if (!install_handler(SIGTERM, actions_->terminate)) {
    error = std::string("cannot install SIGTERM handler: ") +
            std::strerror(errno);
    restore();
    return false;
  }
  terminate_installed_ = true;
  if (virtual_terminal_events) {
    if (!install_handler(SIGUSR1, actions_->release)) {
      error = std::string("cannot install VT release handler: ") +
              std::strerror(errno);
      restore();
      return false;
    }
    release_installed_ = true;
    if (!install_handler(SIGUSR2, actions_->acquire)) {
      error = std::string("cannot install VT acquire handler: ") +
              std::strerror(errno);
      restore();
      return false;
    }
    acquire_installed_ = true;
  }
  return true;
}

SignalEvents SignalRuntime::drain() noexcept {
  SignalEvents events;
  unsigned char tags[32];
  for (;;) {
    const auto count = ::read(pipe_[0], tags, sizeof(tags));
    if (count <= 0) break;
    for (ssize_t index = 0; index < count; ++index) {
      switch (tags[index]) {
        case 'S': events.stop = true; break;
        case 'R': events.virtual_terminal_release = true; break;
        case 'A': events.virtual_terminal_acquire = true; break;
        default: break;
      }
    }
  }
  return events;
}

void SignalRuntime::restore() noexcept {
  if (signal_pipe_fd == pipe_[1]) signal_pipe_fd = -1;
  if (actions_) {
    if (acquire_installed_)
      (void)::sigaction(SIGUSR2, &actions_->acquire, nullptr);
    if (release_installed_)
      (void)::sigaction(SIGUSR1, &actions_->release, nullptr);
    if (terminate_installed_)
      (void)::sigaction(SIGTERM, &actions_->terminate, nullptr);
    if (interrupt_installed_)
      (void)::sigaction(SIGINT, &actions_->interrupt, nullptr);
  }
  acquire_installed_ = false;
  release_installed_ = false;
  terminate_installed_ = false;
  interrupt_installed_ = false;
  if (pipe_[0] >= 0) (void)::close(pipe_[0]);
  if (pipe_[1] >= 0) (void)::close(pipe_[1]);
  pipe_[0] = -1;
  pipe_[1] = -1;
  delete actions_;
  actions_ = nullptr;
}

}  // namespace glasswyrm::compositor
