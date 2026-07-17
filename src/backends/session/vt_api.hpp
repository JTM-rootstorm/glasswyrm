#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace glasswyrm::session {

inline constexpr unsigned kMaximumLinuxVirtualTerminal = 63;
inline constexpr unsigned kLinuxTtyMajor = 4;

struct DeviceIdentity {
  unsigned major{};
  unsigned minor{};
  bool character_device{};
};

struct VirtualTerminalState {
  std::uint16_t active{};
  std::uint16_t signal{};
  std::uint16_t allocated_mask{};
};

struct VirtualTerminalMode {
  std::uint8_t mode{};
  std::uint8_t wait_on_write{};
  std::int16_t release_signal{};
  std::int16_t acquire_signal{};
  std::int16_t forced_release_signal{};
};

[[nodiscard]] bool parse_virtual_terminal_path(std::string_view path,
                                               unsigned& number) noexcept;

class VirtualTerminalApi {
public:
  virtual ~VirtualTerminalApi() = default;

  [[nodiscard]] virtual int open_terminal(std::string_view path) = 0;
  [[nodiscard]] virtual bool identify(int fd, DeviceIdentity& identity) = 0;
  [[nodiscard]] virtual bool get_state(int fd, VirtualTerminalState& state) = 0;
  [[nodiscard]] virtual bool get_mode(int fd, VirtualTerminalMode& mode) = 0;
  [[nodiscard]] virtual bool get_kd_mode(int fd, int& mode) = 0;
  [[nodiscard]] virtual bool get_keyboard_mode(int fd, int& mode) = 0;
  [[nodiscard]] virtual bool activate(int fd, unsigned number) = 0;
  [[nodiscard]] virtual bool wait_until_active(int fd, unsigned number) = 0;
  [[nodiscard]] virtual bool set_process_mode(int fd, int release_signal,
                                              int acquire_signal) = 0;
  [[nodiscard]] virtual bool set_mode(int fd,
                                      const VirtualTerminalMode& mode) = 0;
  [[nodiscard]] virtual bool set_graphics_mode(int fd) = 0;
  [[nodiscard]] virtual bool set_kd_mode(int fd, int mode) = 0;
  [[nodiscard]] virtual bool set_keyboard_mode(int fd, int mode) = 0;
  [[nodiscard]] virtual bool acknowledge_release(int fd) = 0;
  [[nodiscard]] virtual bool acknowledge_acquire(int fd) = 0;
  virtual void close_terminal(int fd) noexcept = 0;
  [[nodiscard]] virtual std::string last_error() const = 0;
};

class LinuxVirtualTerminalApi final : public VirtualTerminalApi {
public:
  [[nodiscard]] int open_terminal(std::string_view path) override;
  [[nodiscard]] bool identify(int fd, DeviceIdentity& identity) override;
  [[nodiscard]] bool get_state(int fd, VirtualTerminalState& state) override;
  [[nodiscard]] bool get_mode(int fd, VirtualTerminalMode& mode) override;
  [[nodiscard]] bool get_kd_mode(int fd, int& mode) override;
  [[nodiscard]] bool get_keyboard_mode(int fd, int& mode) override;
  [[nodiscard]] bool activate(int fd, unsigned number) override;
  [[nodiscard]] bool wait_until_active(int fd, unsigned number) override;
  [[nodiscard]] bool set_process_mode(int fd, int release_signal,
                                      int acquire_signal) override;
  [[nodiscard]] bool set_mode(int fd,
                              const VirtualTerminalMode& mode) override;
  [[nodiscard]] bool set_graphics_mode(int fd) override;
  [[nodiscard]] bool set_kd_mode(int fd, int mode) override;
  [[nodiscard]] bool set_keyboard_mode(int fd, int mode) override;
  [[nodiscard]] bool acknowledge_release(int fd) override;
  [[nodiscard]] bool acknowledge_acquire(int fd) override;
  void close_terminal(int fd) noexcept override;
  [[nodiscard]] std::string last_error() const override;

private:
  void remember_error() noexcept;

  int error_number_{};
};

} // namespace glasswyrm::session
