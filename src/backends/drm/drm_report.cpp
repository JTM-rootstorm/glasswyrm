#include "backends/drm/drm_report.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <linux/fs.h>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

namespace glasswyrm::drm {
namespace {

std::string json_quote(const std::string_view value) {
  std::ostringstream stream;
  stream << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
      case '"': stream << "\\\""; break;
      case '\\': stream << "\\\\"; break;
      case '\b': stream << "\\b"; break;
      case '\f': stream << "\\f"; break;
      case '\n': stream << "\\n"; break;
      case '\r': stream << "\\r"; break;
      case '\t': stream << "\\t"; break;
      default:
        if (byte < 0x20) {
          stream << "\\u00" << std::hex << std::setfill('0') << std::setw(2)
                 << static_cast<unsigned>(byte) << std::dec;
        } else {
          stream << static_cast<char>(byte);
        }
    }
  }
  stream << '"';
  return stream.str();
}

std::string hex64(const std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

const char* boolean(const bool value) { return value ? "true" : "false"; }

const char* api_name(const ReportApiPath api) {
  return api == ReportApiPath::Atomic ? "atomic" : "legacy";
}

template <typename Value>
void array(std::ostringstream& stream, const std::vector<Value>& values) {
  stream << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) stream << ',';
    stream << values[index];
  }
  stream << ']';
}

std::string serialize(const DiscoveryReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"discovery\",\"device\":"
         << json_quote(value.device_path) << ",\"driver\":"
         << json_quote(value.driver_name) << ",\"primary_node\":"
         << boolean(value.primary_node) << ",\"dumb_buffer\":"
         << boolean(value.dumb_buffer_capable) << ",\"atomic\":"
         << boolean(value.atomic_capable) << '}';
  return stream.str();
}

std::string serialize(const SelectionReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"selection\",\"connector\":"
         << json_quote(value.connector_name) << ",\"connector_id\":"
         << value.connector_id << ",\"crtc_id\":" << value.crtc_id
         << ",\"primary_plane_id\":" << value.primary_plane_id
         << ",\"mode\":" << json_quote(value.mode_name) << ",\"width\":"
         << value.width << ",\"height\":" << value.height
         << ",\"refresh_millihz\":" << value.refresh_millihz
         << ",\"api\":" << json_quote(api_name(value.api))
         << ",\"dumb_buffer\":true,\"atomic\":"
         << boolean(value.api == ReportApiPath::Atomic)
         << ",\"framebuffer_format\":" << json_quote(value.framebuffer_format)
         << ",\"buffer_count\":" << value.pitches.size()
         << ",\"pitches\":";
  array(stream, value.pitches);
  stream << ",\"sizes\":";
  array(stream, value.sizes);
  stream << ",\"vt_path\":" << json_quote(value.vt_path)
         << ",\"vt_owned\":" << boolean(value.vt_owned) << '}';
  return stream.str();
}

std::string serialize(const ModesetReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"modeset\",\"commit_id\":" << value.commit_id
         << ",\"generation\":" << value.generation
         << ",\"front_buffer\":" << value.front_buffer_index
         << ",\"framebuffer_id\":" << value.framebuffer_id
         << ",\"canonical_hash\":" << json_quote(hex64(value.canonical_hash))
         << ",\"scanout_hash\":" << json_quote(hex64(value.scanout_hash))
         << ",\"api\":" << json_quote(api_name(value.api)) << '}';
  return stream.str();
}

std::string serialize(const FlipReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"flip\",\"ordinal\":" << value.ordinal
         << ",\"commit_id\":" << value.commit_id << ",\"generation\":"
         << value.generation << ",\"front_buffer\":"
         << value.front_buffer_index << ",\"framebuffer_id\":"
         << value.framebuffer_id << ",\"canonical_hash\":"
         << json_quote(hex64(value.canonical_hash)) << ",\"scanout_hash\":"
         << json_quote(hex64(value.scanout_hash)) << ",\"page_flip_sequence\":"
         << value.page_flip_sequence << ",\"api\":"
         << json_quote(api_name(value.api)) << '}';
  return stream.str();
}

std::string serialize(const VtReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vt\",\"transition\":"
         << json_quote(value.transition == VtTransition::Release ? "release"
                                                                 : "acquire")
         << ",\"master_owned\":" << boolean(value.master_owned)
         << ",\"full_modeset\":" << boolean(value.full_modeset)
         << ",\"committed_hash\":" << json_quote(hex64(value.committed_hash))
         << '}';
  return stream.str();
}

std::string serialize(const RestoreReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"restore\",\"kms\":"
         << boolean(value.kms_restore) << ",\"vt\":"
         << boolean(value.vt_restore) << ",\"master_drop\":"
         << boolean(value.master_drop) << ",\"framebuffer_cleanup\":"
         << boolean(value.framebuffer_cleanup) << '}';
  return stream.str();
}

std::string serialize(const FatalReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"fatal\",\"stage\":" << json_quote(value.stage)
         << ",\"reason\":" << json_quote(value.reason) << ",\"connector\":"
         << json_quote(value.connector_name) << ",\"crtc_id\":" << value.crtc_id
         << ",\"framebuffer_id\":" << value.framebuffer_id
         << ",\"commit_id\":" << value.commit_id << ",\"generation\":"
         << value.generation << '}';
  return stream.str();
}

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

bool valid(const DiscoveryReport& value) {
  return !value.device_path.empty() && !value.driver_name.empty();
}

bool valid(const SelectionReport& value) {
  return !value.connector_name.empty() && value.connector_id != 0 &&
         value.crtc_id != 0 && value.primary_plane_id != 0 &&
         !value.mode_name.empty() && value.width != 0 && value.height != 0 &&
         value.refresh_millihz != 0 && !value.framebuffer_format.empty() &&
         value.pitches.size() == 2 && value.sizes.size() == 2;
}

bool valid(const ModesetReport& value) {
  return value.commit_id != 0 && value.generation != 0 &&
         value.framebuffer_id != 0 &&
         value.canonical_hash == value.scanout_hash;
}

bool valid(const FlipReport& value) {
  return value.ordinal != 0 && value.commit_id != 0 && value.generation != 0 &&
         value.framebuffer_id != 0 && value.page_flip_sequence != 0 &&
         value.canonical_hash == value.scanout_hash;
}

bool valid(const VtReport& value) {
  return value.transition == VtTransition::Release
             ? !value.master_owned && !value.full_modeset
             : value.master_owned && value.full_modeset;
}

bool valid(const RestoreReport&) { return true; }

bool valid(const FatalReport& value) {
  return !value.stage.empty() && !value.reason.empty();
}

bool valid(const DrmReportRecord& record) {
  return std::visit([](const auto& value) { return valid(value); }, record);
}

bool identity(const std::filesystem::path& path, struct stat& status,
              std::string& error) {
  if (::lstat(path.c_str(), &status) != 0) {
    error = std::string("cannot inspect DRM report path: ") +
            std::strerror(errno);
    return false;
  }
  return true;
}

} // namespace

std::string serialize_report_record(const DrmReportRecord& record) {
  return std::visit([](const auto& value) { return serialize(value); }, record) +
         '\n';
}

StagedDrmReport::~StagedDrmReport() { discard(); }

StagedDrmReport::StagedDrmReport(StagedDrmReport&& other) noexcept
    : temporary_path_(std::move(other.temporary_path_)),
      final_path_(std::move(other.final_path_)),
      contents_(std::move(other.contents_)),
      base_generation_(other.base_generation_),
      active_(std::exchange(other.active_, false)) {}

StagedDrmReport& StagedDrmReport::operator=(StagedDrmReport&& other) noexcept {
  if (this == &other) return *this;
  discard();
  temporary_path_ = std::move(other.temporary_path_);
  final_path_ = std::move(other.final_path_);
  contents_ = std::move(other.contents_);
  base_generation_ = other.base_generation_;
  active_ = std::exchange(other.active_, false);
  return *this;
}

void StagedDrmReport::discard() noexcept {
  if (!active_) return;
  std::error_code ignored;
  std::filesystem::remove(temporary_path_, ignored);
  active_ = false;
}

bool DrmReport::initialize(std::string& error) {
  error.clear();
  if (initialized_) {
    error = "DRM report is already initialized";
    return false;
  }
  if (path_.empty() || path_.filename().empty() || path_.filename() == "." ||
      path_.filename() == "..") {
    error = "DRM report path must name a file";
    return false;
  }
  parent_ = path_.parent_path();
  if (parent_.empty()) parent_ = ".";
  struct stat parent_status {};
  if (!identity(parent_, parent_status, error)) return false;
  if (!S_ISDIR(parent_status.st_mode) || S_ISLNK(parent_status.st_mode)) {
    error = "DRM report parent must be a real directory";
    return false;
  }
  parent_identity_ = {static_cast<std::uint64_t>(parent_status.st_dev),
                      static_cast<std::uint64_t>(parent_status.st_ino)};
  struct stat target_status {};
  if (::lstat(path_.c_str(), &target_status) == 0 || errno != ENOENT) {
    error = "DRM report path must not already exist";
    return false;
  }
  initialized_ = true;
  return true;
}

bool DrmReport::validate_parent(std::string& error) const {
  struct stat status {};
  if (!identity(parent_, status, error)) return false;
  if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode) ||
      static_cast<std::uint64_t>(status.st_dev) != parent_identity_.device ||
      static_cast<std::uint64_t>(status.st_ino) != parent_identity_.inode) {
    error = "DRM report parent directory was replaced";
    return false;
  }
  return true;
}

bool DrmReport::validate_target(std::string& error) const {
  if (!validate_parent(error)) return false;
  struct stat status {};
  if (generation_ == 0) {
    if (::lstat(path_.c_str(), &status) == 0 || errno != ENOENT) {
      error = "DRM report target appeared after initialization";
      return false;
    }
    return true;
  }
  if (!identity(path_, status, error)) return false;
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1 ||
      static_cast<std::uint64_t>(status.st_dev) != target_identity_.device ||
      static_cast<std::uint64_t>(status.st_ino) != target_identity_.inode) {
    error = "DRM report target was replaced";
    return false;
  }
  return true;
}

bool DrmReport::capture_target_identity(std::string& error) {
  struct stat status {};
  if (!identity(path_, status, error)) return false;
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1) {
    error = "published DRM report is not a private regular file";
    return false;
  }
  target_identity_ = {static_cast<std::uint64_t>(status.st_dev),
                      static_cast<std::uint64_t>(status.st_ino)};
  return true;
}

bool DrmReport::stage(const DrmReportRecord& record,
                      StagedDrmReport& staged, std::string& error) {
  return stage(std::span(&record, 1), staged, error);
}

bool DrmReport::stage(const std::span<const DrmReportRecord> records,
                      StagedDrmReport& staged, std::string& error) {
  error.clear();
  if (!initialized_) {
    error = "DRM report is not initialized";
    return false;
  }
  if (records.empty()) {
    error = "cannot stage an empty DRM report update";
    return false;
  }
  if (!validate_target(error)) return false;

  std::string contents = committed_contents_;
  for (const auto& record : records) {
    if (!valid(record)) {
      error = "invalid or internally inconsistent DRM report record";
      return false;
    }
    contents += serialize_report_record(record);
  }
  const auto temporary =
      parent_ / ("." + path_.filename().string() + ".stage." +
                 std::to_string(static_cast<long long>(::getpid())) + "." +
                 std::to_string(generation_ + 1));
  const int fd = ::open(temporary.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                        0600);
  if (fd < 0) {
    error = std::string("cannot create staged DRM report: ") +
            std::strerror(errno);
    return false;
  }
  bool success = write_all(fd, contents, error);
  if (success && ::fsync(fd) != 0) {
    error = std::string("DRM report fsync failed: ") + std::strerror(errno);
    success = false;
  }
  if (::close(fd) != 0 && success) {
    error = std::string("DRM report close failed: ") + std::strerror(errno);
    success = false;
  }
  if (!success) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }

  StagedDrmReport replacement;
  replacement.temporary_path_ = temporary;
  replacement.final_path_ = path_;
  replacement.contents_ = std::move(contents);
  replacement.base_generation_ = generation_;
  replacement.active_ = true;
  staged = std::move(replacement);
  return true;
}

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

  const auto exchange_paths = [&]() {
    return static_cast<int>(::syscall(
        SYS_renameat2, AT_FDCWD, staged.temporary_path_.c_str(), AT_FDCWD,
        path_.c_str(), RENAME_EXCHANGE));
  };

  if (generation_ == 0) {
    const int result = static_cast<int>(::syscall(
        SYS_renameat2, AT_FDCWD, staged.temporary_path_.c_str(), AT_FDCWD,
        path_.c_str(), RENAME_NOREPLACE));
    if (result != 0) {
      error = std::string("DRM report atomic publication failed: ") +
              std::strerror(errno);
      staged.discard();
      return false;
    }
  } else {
    if (exchange_paths() != 0) {
      error = std::string("DRM report atomic exchange failed: ") +
              std::strerror(errno);
      staged.discard();
      return false;
    }

    struct stat displaced {};
    const bool expected_target =
        ::lstat(staged.temporary_path_.c_str(), &displaced) == 0 &&
        S_ISREG(displaced.st_mode) && displaced.st_nlink == 1 &&
        static_cast<std::uint64_t>(displaced.st_dev) ==
            target_identity_.device &&
        static_cast<std::uint64_t>(displaced.st_ino) == target_identity_.inode;
    if (!expected_target) {
      error = "DRM report target raced with atomic publication";
      if (exchange_paths() != 0) {
        error += std::string("; rollback failed: ") + std::strerror(errno);
        // The displaced path may belong to another process. Preserve it rather
        // than allowing the staged transaction destructor to unlink it.
        staged.active_ = false;
      } else {
        staged.discard();
      }
      return false;
    }
    if (::unlink(staged.temporary_path_.c_str()) != 0) {
      error = std::string("cannot remove superseded DRM report: ") +
              std::strerror(errno);
      if (exchange_paths() != 0) {
        error += std::string("; rollback failed: ") + std::strerror(errno);
        staged.active_ = false;
      } else {
        staged.discard();
      }
      return false;
    }
  }
  if (!capture_target_identity(error)) {
    staged.active_ = false;
    return false;
  }
  committed_contents_ = std::move(staged.contents_);
  ++generation_;
  staged.active_ = false;
  return true;
}

void DrmReport::abort(StagedDrmReport& staged) const noexcept {
  staged.discard();
}

} // namespace glasswyrm::drm
