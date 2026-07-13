#pragma once

#include <string>

namespace glasswyrm::session {

class FileDescriptorApi {
public:
  virtual ~FileDescriptorApi() = default;

  [[nodiscard]] virtual int duplicate_close_on_exec(int fd) = 0;
  virtual void close_fd(int fd) noexcept = 0;
  [[nodiscard]] virtual std::string last_error() const = 0;
};

class PosixFileDescriptorApi final : public FileDescriptorApi {
public:
  [[nodiscard]] int duplicate_close_on_exec(int fd) override;
  void close_fd(int fd) noexcept override;
  [[nodiscard]] std::string last_error() const override;

private:
  int error_number_{};
};

class ExternalDeviceSession final {
public:
  explicit ExternalDeviceSession(FileDescriptorApi& api) noexcept : api_(api) {}
  ~ExternalDeviceSession();

  ExternalDeviceSession(const ExternalDeviceSession&) = delete;
  ExternalDeviceSession& operator=(const ExternalDeviceSession&) = delete;

  [[nodiscard]] bool adopt(int caller_fd, std::string& error);
  void reset() noexcept;

  [[nodiscard]] int device_fd() const noexcept { return device_fd_; }
  [[nodiscard]] bool owns_virtual_terminal() const noexcept { return false; }

private:
  FileDescriptorApi& api_;
  int device_fd_{-1};
};

} // namespace glasswyrm::session
