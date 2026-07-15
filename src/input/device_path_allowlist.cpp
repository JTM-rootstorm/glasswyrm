#include "input/device_path_allowlist.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

namespace glasswyrm::input {
namespace {

std::optional<std::string> canonicalize(std::string_view path,
                                        std::string &error) {
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    error = "input device path is empty or contains NUL";
    return std::nullopt;
  }
  std::string owned(path);
  char *resolved = ::realpath(owned.c_str(), nullptr);
  if (resolved == nullptr) {
    error = "canonicalize input device " + owned + ": " +
            std::strerror(errno);
    return std::nullopt;
  }
  std::string result(resolved);
  std::free(resolved);
  return result;
}

}  // namespace

std::optional<DevicePathAllowlist> DevicePathAllowlist::create(
    std::span<const std::string> paths, std::string &error) {
  if (paths.empty()) {
    error = "at least one explicit input device path is required";
    return std::nullopt;
  }
  std::vector<std::string> canonical;
  canonical.reserve(paths.size());
  for (const auto &path : paths) {
    auto resolved = canonicalize(path, error);
    if (!resolved) return std::nullopt;
    canonical.push_back(std::move(*resolved));
  }
  std::sort(canonical.begin(), canonical.end());
  canonical.erase(std::unique(canonical.begin(), canonical.end()),
                  canonical.end());
  error.clear();
  return DevicePathAllowlist(std::move(canonical));
}

int DevicePathAllowlist::open_restricted(
    const std::string_view requested_path,
    const int required_flags) const noexcept {
  if (requested_path.empty() ||
      requested_path.find('\0') != std::string_view::npos)
    return -EINVAL;
  std::string ignored_error;
  const auto canonical = canonicalize(requested_path, ignored_error);
  if (!canonical) return -errno;
  if (!std::binary_search(paths_.begin(), paths_.end(), *canonical))
    return -EACCES;

  int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOCTTY
  flags |= required_flags & O_NOCTTY;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = ::open(canonical->c_str(), flags);
  return fd < 0 ? -errno : fd;
}

void DevicePathAllowlist::close_restricted(const int fd) const noexcept {
  if (fd >= 0) (void)::close(fd);
}

}  // namespace glasswyrm::input
