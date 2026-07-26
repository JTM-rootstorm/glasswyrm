#include "backends/drm/drm_report.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
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

std::uint64_t copy_ratio_ppm(const std::uint64_t copied,
                             const std::uint64_t full) noexcept {
  if (full == 0) return 0;
  const auto scaled = static_cast<unsigned __int128>(copied) * 1000000U;
  return static_cast<std::uint64_t>(scaled / full);
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
  stream << "{\"record\":\"modeset\",\"ordinal\":" << value.ordinal
         << ",\"commit_id\":" << value.commit_id
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

std::string serialize(const DamageCopyReport& value) {
  const auto ratio_ppm = copy_ratio_ppm(value.cumulative_copied_bytes,
                                        value.cumulative_full_frame_bytes);
  std::ostringstream stream;
  stream << "{\"record\":\"damage-copy\",\"generation\":"
         << value.generation << ",\"buffer\":" << value.buffer_index
         << ",\"framebuffer_id\":" << value.framebuffer_id
         << ",\"full_frame_bytes\":" << value.full_frame_bytes
         << ",\"copied_bytes\":" << value.copied_bytes
         << ",\"copy_rectangles\":[";
  for (std::size_t index = 0; index < value.rectangles.size(); ++index) {
    if (index != 0) stream << ',';
    const auto& rectangle = value.rectangles[index];
    stream << "{\"x\":" << rectangle.x << ",\"y\":" << rectangle.y
           << ",\"width\":" << rectangle.width << ",\"height\":"
           << rectangle.height << '}';
  }
  stream << "],\"history_span\":" << value.history_span
         << ",\"drm_copied_bytes\":" << value.drm_copied_bytes
         << ",\"copy_nanoseconds\":" << value.copy_nanoseconds
         << ",\"parity_verified_bytes\":"
         << value.parity_verified_bytes
         << ",\"scanout_readback_bytes\":"
         << value.scanout_readback_bytes
         << ",\"parity_nanoseconds\":" << value.parity_nanoseconds
         << ",\"full_copy_reason\":"
         << json_quote(full_copy_reason_name(value.full_copy_reason))
         << ",\"cumulative_full_frame_bytes\":"
         << value.cumulative_full_frame_bytes
         << ",\"cumulative_copied_bytes\":"
         << value.cumulative_copied_bytes
         << ",\"cumulative_copy_ratio_ppm\":" << ratio_ppm << '}';
  return stream.str();
}

std::string serialize(const EvidenceStreamReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"evidence-stream\",\"output_id\":"
         << value.evidence.output_id << ",\"commit_id\":"
         << value.evidence.commit_id << ",\"generation\":"
         << value.evidence.generation << ",\"presentation_token\":"
         << value.evidence.presentation_token << ",\"stream\":"
         << value.stream << '}';
  return stream.str();
}

std::string serialize(const EvidenceSealReport& value) {
  std::ostringstream stream;
  stream << "{\"record\":\"evidence-seal\",\"output_id\":"
         << value.evidence.output_id << ",\"commit_id\":"
         << value.evidence.commit_id << ",\"generation\":"
         << value.evidence.generation << ",\"presentation_token\":"
         << value.evidence.presentation_token << ",\"required_streams\":"
         << value.required_streams << ",\"committed_streams\":"
         << value.committed_streams << ",\"mirror_frame\":"
         << value.mirror_frame << ",\"mirror_fnv1a64\":"
         << json_quote(hex64(value.mirror_fnv1a64)) << ",\"mirror_file\":"
         << json_quote(value.mirror_file) << '}';
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

std::string serialize(const DrmVrrReportRecord& value) {
  return serialize_drm_vrr_report_record(value);
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
         value.crtc_id != 0 &&
         (value.api == ReportApiPath::Legacy ||
          value.primary_plane_id != 0) &&
         !value.mode_name.empty() && value.width != 0 && value.height != 0 &&
         value.refresh_millihz != 0 && !value.framebuffer_format.empty() &&
         value.pitches.size() == 2 && value.sizes.size() == 2;
}

bool valid(const ModesetReport& value) {
  return value.ordinal != 0 && value.commit_id != 0 && value.generation != 0 &&
         value.framebuffer_id != 0 &&
         value.canonical_hash == value.scanout_hash;
}

bool valid(const FlipReport& value) {
  return value.ordinal != 0 && value.commit_id != 0 && value.generation != 0 &&
         value.framebuffer_id != 0 &&
         value.canonical_hash == value.scanout_hash;
}

bool valid(const VtReport& value) {
  return value.transition == VtTransition::Release
             ? !value.master_owned && !value.full_modeset
             : value.master_owned && value.full_modeset;
}

bool valid(const DamageCopyReport& value) {
  return value.generation != 0 && value.framebuffer_id != 0 &&
         value.full_frame_bytes != 0 &&
         value.copied_bytes <= value.full_frame_bytes &&
         value.cumulative_copied_bytes <=
             value.cumulative_full_frame_bytes &&
         !value.rectangles.empty() &&
         value.drm_copied_bytes >= value.copied_bytes &&
         value.parity_verified_bytes >= value.full_frame_bytes &&
         value.scanout_readback_bytes <= value.parity_verified_bytes &&
         (value.full_copy_reason == FullCopyReason::None ||
          value.scanout_readback_bytes >= value.full_frame_bytes) &&
         (value.full_copy_reason == FullCopyReason::None
              ? value.history_span != 0
              : value.copied_bytes == value.full_frame_bytes);
}

bool valid(const EvidenceStreamReport& value) {
  return value.evidence.output_id != 0 && value.evidence.commit_id != 0 &&
         value.evidence.generation != 0 &&
         value.evidence.presentation_token != 0 &&
         (value.stream == kEvidenceStreamDrmReport ||
          value.stream == kEvidenceStreamVrrReport);
}

bool valid(const EvidenceSealReport& value) {
  const auto& evidence = value.evidence;
  const bool mirror_required =
      (value.required_streams & kEvidenceStreamMirror) != 0;
  return evidence.output_id != 0 && evidence.commit_id != 0 &&
         evidence.generation != 0 && evidence.presentation_token != 0 &&
         (value.required_streams & ~kKnownEvidenceStreamMask) == 0 &&
         (value.required_streams &
          (kEvidenceStreamDrmReport | kEvidenceStreamVrrReport)) ==
             (kEvidenceStreamDrmReport | kEvidenceStreamVrrReport) &&
         value.committed_streams == value.required_streams &&
         (mirror_required
              ? value.mirror_frame != 0 && !value.mirror_file.empty()
              : value.mirror_frame == 0 && value.mirror_fnv1a64 == 0 &&
                    value.mirror_file.empty());
}

bool valid(const RestoreReport&) { return true; }

bool valid(const FatalReport& value) {
  return !value.stage.empty() && !value.reason.empty();
}

bool valid(const DrmVrrReportRecord& value) {
  return valid_drm_vrr_report_record(value);
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
  temporary_path_.clear();
  final_path_.clear();
  contents_.clear();
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
      static_cast<std::uint64_t>(status.st_ino) != target_identity_.inode ||
      static_cast<std::uint64_t>(status.st_size) != committed_size_) {
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
  if (poisoned_) {
    error = "DRM report is unavailable after an ambiguous write failure";
    return false;
  }
  if (records.empty()) {
    error = "cannot stage an empty DRM report update";
    return false;
  }
  if (!validate_target(error)) return false;

  std::string contents;
  for (const auto& record : records) {
    if (!valid(record)) {
      error = "invalid or internally inconsistent DRM report record";
      return false;
    }
    contents += serialize_report_record(record);
  }

  StagedDrmReport replacement;
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
    if (first) {
      unlink_if_identity(path_, status);
    } else {
      poisoned_ = true;
    }
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

void DrmReport::abort(StagedDrmReport& staged) const noexcept {
  staged.discard();
}

} // namespace glasswyrm::drm
