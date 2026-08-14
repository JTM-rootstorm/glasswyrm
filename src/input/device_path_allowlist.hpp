#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace glasswyrm::input {

class DevicePathAllowlist {
 public:
  [[nodiscard]] static std::optional<DevicePathAllowlist> create(
      std::span<const std::string> paths, std::string &error);

  [[nodiscard]] int open_restricted(std::string_view requested_path,
                                    int required_flags) const noexcept;
  void close_restricted(int fd) const noexcept;
  [[nodiscard]] const std::vector<std::string> &paths() const noexcept {
    return paths_;
  }

 private:
  struct DeviceIdentity {
    dev_t device{};
    ino_t inode{};
    dev_t special_device{};
    mode_t mode{};
  };

  DevicePathAllowlist(std::vector<std::string> paths,
                      std::vector<DeviceIdentity> identities)
      : paths_(std::move(paths)), identities_(std::move(identities)) {}

  std::vector<std::string> paths_;
  std::vector<DeviceIdentity> identities_;
};

}  // namespace glasswyrm::input
