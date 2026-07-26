#include "backends/drm/drm_report.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace glasswyrm::drm {
namespace {

bool write_all(const int fd, std::string_view bytes, std::string& error) {
  while (!bytes.empty()) {
    const auto written = ::write(fd, bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) continue;
      error = std::string("DRM report write failed: ") + std::strerror(errno);
      return false;
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
  return true;
}

void unlink_if_identity(const std::filesystem::path& path,
                        const struct stat& expected) noexcept {
  struct stat current {};
  if (::lstat(path.c_str(), &current) == 0 &&
      static_cast<std::uint64_t>(current.st_dev) ==
          static_cast<std::uint64_t>(expected.st_dev) &&
      static_cast<std::uint64_t>(current.st_ino) ==
          static_cast<std::uint64_t>(expected.st_ino))
    (void)::unlink(path.c_str());
}

}  // namespace

bool DrmReport::commit(StagedDrmReport& staged, std::string& error) {
  error.clear();
  if (!staged.active_ || staged.final_path_ != path_ ||
      staged.base_generation_ != generation_) {
    error = "DRM report update is not the current staged transaction";
    return false;
  }
  if (!validate_target(error)) {
    staged.discard();
    return false;
  }

  const auto hook = std::exchange(before_publish_hook_, nullptr);
  const auto hook_context = std::exchange(before_publish_context_, nullptr);
  if (hook) hook(hook_context);

  const bool first = generation_ == 0;
  const int flags = O_WRONLY | O_CLOEXEC | O_NOFOLLOW |
                    (first ? O_CREAT | O_EXCL : O_APPEND);
  const int fd = ::open(path_.c_str(), flags, 0600);
  if (fd < 0) {
    error = std::string(first ? "DRM report publication failed: "
                              : "DRM report append open failed: ") +
            std::strerror(errno);
    staged.discard();
    return false;
  }

  struct stat status {};
  const bool inspected = ::fstat(fd, &status) == 0;
  const bool expected_target =
      inspected && S_ISREG(status.st_mode) && status.st_nlink == 1 &&
      (first ||
       (static_cast<std::uint64_t>(status.st_dev) ==
            target_identity_.device &&
        static_cast<std::uint64_t>(status.st_ino) ==
            target_identity_.inode &&
        static_cast<std::uint64_t>(status.st_size) == committed_size_));
  if (!expected_target) {
    error = "DRM report target raced with publication";
    (void)::close(fd);
    if (first && inspected) unlink_if_identity(path_, status);
    staged.discard();
    return false;
  }

  const bool wrote = write_all(fd, staged.contents_, error);
  const bool closed = ::close(fd) == 0;
  if (!wrote || !closed) {
    if (error.empty())
      error = std::string("DRM report close failed: ") + std::strerror(errno);
    if (first)
      unlink_if_identity(path_, status);
    else
      poisoned_ = true;
    staged.discard();
    return false;
  }
  if (first && !capture_target_identity(error)) {
    poisoned_ = true;
    staged.discard();
    return false;
  }
  committed_size_ += staged.contents_.size();
  ++generation_;
  dirty_ = true;
  staged.active_ = false;
  return true;
}

bool DrmReport::flush(std::string& error) {
  error.clear();
  if (!initialized_) {
    error = "DRM report is not initialized";
    return false;
  }
  if (poisoned_) {
    error = "DRM report is unavailable after an ambiguous write failure";
    return false;
  }
  if (!dirty_) return true;
  if (!validate_target(error)) return false;
  const int fd = ::open(path_.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    error = std::string("DRM report flush open failed: ") +
            std::strerror(errno);
    return false;
  }
  struct stat status {};
  const bool expected_target =
      ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
      status.st_nlink == 1 &&
      static_cast<std::uint64_t>(status.st_dev) == target_identity_.device &&
      static_cast<std::uint64_t>(status.st_ino) == target_identity_.inode &&
      static_cast<std::uint64_t>(status.st_size) == committed_size_;
  if (!expected_target) {
    error = "DRM report target raced with flush";
    (void)::close(fd);
    return false;
  }
  bool success = ::fsync(fd) == 0;
  if (!success)
    error = std::string("DRM report fsync failed: ") + std::strerror(errno);
  if (::close(fd) != 0 && success) {
    error = std::string("DRM report close failed: ") + std::strerror(errno);
    success = false;
  }
  if (success) dirty_ = false;
  return success;
}

}  // namespace glasswyrm::drm
