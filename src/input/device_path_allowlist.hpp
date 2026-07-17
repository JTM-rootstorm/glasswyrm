#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
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
  explicit DevicePathAllowlist(std::vector<std::string> paths)
      : paths_(std::move(paths)) {}

  std::vector<std::string> paths_;
};

}  // namespace glasswyrm::input
