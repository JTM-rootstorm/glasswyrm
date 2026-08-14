#include "input/device_path_allowlist.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
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
  std::vector<DeviceIdentity> identities;
  identities.reserve(canonical.size());
  for (const auto &path : canonical) {
    const int fd = ::open(path.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
      error = "inspect input device " + path + ": " + std::strerror(errno);
      return std::nullopt;
    }
    struct stat status {};
    const int inspected = ::fstat(fd, &status);
    const int inspect_error = errno;
    (void)::close(fd);
    if (inspected != 0) {
      error = "inspect input device " + path + ": " +
              std::strerror(inspect_error);
      return std::nullopt;
    }
    identities.push_back(
        {status.st_dev, status.st_ino, status.st_rdev, status.st_mode});
  }
  error.clear();
  return DevicePathAllowlist(std::move(canonical), std::move(identities));
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
  const auto path = std::lower_bound(paths_.begin(), paths_.end(), *canonical);
  if (path == paths_.end() || *path != *canonical)
    return -EACCES;

  int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOCTTY
  flags |= required_flags & O_NOCTTY;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = ::open(canonical->c_str(), flags);
  if (fd < 0) return -errno;
  struct stat status {};
  const auto &expected =
      identities_[static_cast<std::size_t>(path - paths_.begin())];
  if (::fstat(fd, &status) != 0 || status.st_dev != expected.device ||
      status.st_ino != expected.inode ||
      status.st_rdev != expected.special_device ||
      (status.st_mode & S_IFMT) != (expected.mode & S_IFMT)) {
    (void)::close(fd);
    return -EACCES;
  }
  return fd;
}

void DevicePathAllowlist::close_restricted(const int fd) const noexcept {
  if (fd >= 0) (void)::close(fd);
}

}  // namespace glasswyrm::input
