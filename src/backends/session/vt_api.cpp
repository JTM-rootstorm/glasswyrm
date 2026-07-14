#include "backends/session/vt_api.hpp"

#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace glasswyrm::session {
namespace {

VirtualTerminalMode from_linux_mode(const vt_mode& mode) {
  return {static_cast<std::uint8_t>(mode.mode),
          static_cast<std::uint8_t>(mode.waitv), mode.relsig, mode.acqsig,
          mode.frsig};
}

vt_mode to_linux_mode(const VirtualTerminalMode& mode) {
  vt_mode native{};
  native.mode = mode.mode;
  native.waitv = mode.wait_on_write;
  native.relsig = mode.release_signal;
  native.acqsig = mode.acquire_signal;
  native.frsig = mode.forced_release_signal;
  return native;
}

} // namespace

bool parse_virtual_terminal_path(const std::string_view path,
                                 unsigned& number) noexcept {
  constexpr std::string_view prefix = "/dev/tty";
  number = 0;
  if (!path.starts_with(prefix) || path.size() == prefix.size()) return false;
  const auto digits = path.substr(prefix.size());
  if (digits.size() > 1 && digits.front() == '0') return false;
  unsigned parsed = 0;
  const auto result =
      std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
      parsed == 0 || parsed > kMaximumLinuxVirtualTerminal)
    return false;
  number = parsed;
  return true;
}

int LinuxVirtualTerminalApi::open_terminal(const std::string_view path) {
  const std::string terminated(path);
  const int fd = ::open(terminated.c_str(),
                        O_RDWR | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
  if (fd < 0) remember_error();
  return fd;
}

bool LinuxVirtualTerminalApi::identify(const int fd,
                                       DeviceIdentity& identity) {
  struct stat status {};
  if (::fstat(fd, &status) != 0) {
    remember_error();
    return false;
  }
  identity.character_device = S_ISCHR(status.st_mode);
  identity.major = static_cast<unsigned>(::major(status.st_rdev));
  identity.minor = static_cast<unsigned>(::minor(status.st_rdev));
  return true;
}

bool LinuxVirtualTerminalApi::get_state(const int fd,
                                        VirtualTerminalState& state) {
  vt_stat native{};
  if (::ioctl(fd, VT_GETSTATE, &native) != 0) {
    remember_error();
    return false;
  }
  state = {native.v_active, native.v_signal, native.v_state};
  return true;
}

bool LinuxVirtualTerminalApi::get_mode(const int fd,
                                       VirtualTerminalMode& mode) {
  vt_mode native{};
  if (::ioctl(fd, VT_GETMODE, &native) != 0) {
    remember_error();
    return false;
  }
  mode = from_linux_mode(native);
  return true;
}

bool LinuxVirtualTerminalApi::get_kd_mode(const int fd, int& mode) {
  if (::ioctl(fd, KDGETMODE, &mode) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::activate(const int fd, const unsigned number) {
  if (::ioctl(fd, VT_ACTIVATE, number) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::wait_until_active(const int fd,
                                                const unsigned number) {
  if (::ioctl(fd, VT_WAITACTIVE, number) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::set_process_mode(const int fd,
                                               const int release_signal,
                                               const int acquire_signal) {
  VirtualTerminalMode mode{};
  mode.mode = VT_PROCESS;
  mode.release_signal = static_cast<std::int16_t>(release_signal);
  mode.acquire_signal = static_cast<std::int16_t>(acquire_signal);
  return set_mode(fd, mode);
}

bool LinuxVirtualTerminalApi::set_mode(
    const int fd, const VirtualTerminalMode& mode) {
  auto native = to_linux_mode(mode);
  if (::ioctl(fd, VT_SETMODE, &native) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::set_graphics_mode(const int fd) {
  return set_kd_mode(fd, KD_GRAPHICS);
}

bool LinuxVirtualTerminalApi::set_kd_mode(const int fd, const int mode) {
  if (::ioctl(fd, KDSETMODE, mode) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::acknowledge_release(const int fd) {
  if (::ioctl(fd, VT_RELDISP, 1) != 0) {
    remember_error();
    return false;
  }
  return true;
}

bool LinuxVirtualTerminalApi::acknowledge_acquire(const int fd) {
  if (::ioctl(fd, VT_RELDISP, VT_ACKACQ) != 0) {
    remember_error();
    return false;
  }
  return true;
}

void LinuxVirtualTerminalApi::close_terminal(const int fd) noexcept {
  if (fd >= 0) (void)::close(fd);
}

std::string LinuxVirtualTerminalApi::last_error() const {
  return error_number_ == 0 ? std::string{} : std::strerror(error_number_);
}

void LinuxVirtualTerminalApi::remember_error() noexcept {
  error_number_ = errno;
}

} // namespace glasswyrm::session
