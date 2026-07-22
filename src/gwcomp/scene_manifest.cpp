#include "gwcomp/scene_manifest.hpp"

#include <glasswyrm/ipc.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace gw::compositor {
namespace {

constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

struct PayloadDeleter {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};

class PosixSceneManifestIo final : public SceneManifestIo {
public:
  int open(const char *path, const int flags,
           const mode_t mode) const override {
    return ::open(path, flags, mode);
  }

  int stat(const int fd, struct stat *status) const override {
    return ::fstat(fd, status);
  }

  int lock(const int fd, const int operation) const override {
    return ::flock(fd, operation);
  }

  ssize_t write(const int fd, const void *data,
                const std::size_t size) const override {
    return ::write(fd, data, size);
  }

  ssize_t read_at(const int fd, void *data, const std::size_t size,
                  const off_t offset) const override {
    return ::pread(fd, data, size, offset);
  }

  int synchronize(const int fd) const override { return ::fdatasync(fd); }

  int truncate(const int fd, const off_t size) const override {
    return ::ftruncate(fd, size);
  }

  int close(const int fd) const override { return ::close(fd); }
};

std::shared_ptr<const SceneManifestIo> default_io() {
  static const auto io = std::make_shared<PosixSceneManifestIo>();
  return io;
}

void hash_bytes(std::uint64_t &hash, std::span<const std::uint8_t> bytes) {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
}

template <class Value, class Encoder>
bool hash_contract(std::uint64_t &hash, const Value &value, Encoder encoder,
                   std::string &error) {
  gwipc_contract_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) {
    error = "scene contract encoding failed";
    return false;
  }
  const std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload.get(), &size);
  hash_bytes(hash, {bytes, size});
  return true;
}

const char *applied(gwipc_policy_applied_state value) {
  switch (value) {
  case GWIPC_POLICY_APPLIED_MAXIMIZED:
    return "Maximized";
  case GWIPC_POLICY_APPLIED_FULLSCREEN:
    return "Fullscreen";
  case GWIPC_POLICY_APPLIED_MINIMIZED:
    return "Minimized";
  default:
    return "Normal";
  }
}

const char *tri(gwipc_tri_state value) {
  switch (value) {
  case GWIPC_TRI_STATE_FALSE:
    return "False";
  case GWIPC_TRI_STATE_TRUE:
    return "True";
  default:
    return "Unknown";
  }
}

const char *boolean(bool value) { return value ? "true" : "false"; }

bool cursor_surface(const gwipc_surface_upsert& surface) {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
}

std::vector<std::uint64_t> manifest_order(const Scene &scene) {
  std::vector<std::uint64_t> visible;
  std::vector<std::uint64_t> hidden;
  for (const auto &[id, surface] : scene.surfaces) {
    if (cursor_surface(surface)) continue;
    (surface.visible ? visible : hidden).push_back(id);
  }
  std::ranges::sort(visible, [&](const auto left, const auto right) {
    const auto &a = scene.surfaces.at(left);
    const auto &b = scene.surfaces.at(right);
    return a.stacking < b.stacking ||
           (a.stacking == b.stacking && a.surface_id < b.surface_id);
  });
  std::ranges::sort(hidden, [&](const auto left, const auto right) {
    const auto &a = scene.surfaces.at(left);
    const auto &b = scene.surfaces.at(right);
    return a.x11_window_id < b.x11_window_id ||
           (a.x11_window_id == b.x11_window_id && a.surface_id < b.surface_id);
  });
  visible.insert(visible.end(), hidden.begin(), hidden.end());
  return visible;
}

struct WriteResult {
  bool complete{};
  bool changed{};
  int error_number{};
};

WriteResult write_all(const SceneManifestIo &io, const int fd,
                      const std::string_view bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count =
        io.write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count > 0)
      offset += static_cast<std::size_t>(count);
    else if (count < 0 && errno == EINTR)
      continue;
    else {
      const int error_number = count == 0 ? EIO : errno;
      return {false, offset != 0, error_number == 0 ? EIO : error_number};
    }
  }
  return {true, !bytes.empty(), 0};
}

struct PublishFailure {
  std::string stage;
  int error_number{};
  std::string detail;
};

PublishFailure errno_failure(std::string stage, const int error_number) {
  return {std::move(stage), error_number == 0 ? EIO : error_number, {}};
}

std::string describe_failure(const PublishFailure &failure) {
  if (!failure.detail.empty())
    return failure.stage + ": " + failure.detail;
  return failure.stage + ": " + std::strerror(failure.error_number);
}

void add_secondary(std::vector<std::string> &secondary,
                   const std::string_view stage, const int error_number) {
  secondary.push_back(std::string(stage) + ": " +
                      std::strerror(error_number == 0 ? EIO : error_number));
}

std::string combined_error(const PublishFailure &primary,
                           const std::vector<std::string> &secondary) {
  std::string result = describe_failure(primary);
  for (const auto &detail : secondary)
    result += "; " + detail;
  return result;
}

bool tail_matches(const SceneManifestIo &io, const int fd,
                  const off_t file_size, const std::string_view expected,
                  int &error_number) {
  if (expected.size() > static_cast<std::uint64_t>(file_size))
    return false;
  std::string bytes(expected.size(), '\0');
  std::size_t offset = 0;
  const auto start = file_size - static_cast<off_t>(expected.size());
  while (offset < bytes.size()) {
    const auto count =
        io.read_at(fd, bytes.data() + offset, bytes.size() - offset,
                   start + static_cast<off_t>(offset));
    if (count > 0)
      offset += static_cast<std::size_t>(count);
    else if (count < 0 && errno == EINTR)
      continue;
    else {
      error_number = count == 0 ? EIO : (errno == 0 ? EIO : errno);
      return false;
    }
  }
  return bytes == expected;
}

} // namespace

SceneManifest::SceneManifest(std::filesystem::path path,
                             std::shared_ptr<const SceneManifestIo> io)
    : path_(std::move(path)), io_(io ? std::move(io) : default_io()) {}

bool SceneManifest::describe(const std::uint64_t commit_id,
                             const std::uint64_t generation, const Scene &scene,
                             SceneManifestResult &result, std::string &json,
                             std::string &error) {
  error.clear();
  if (!scene.output) {
    error = "scene manifest requires an output";
    return false;
  }
  auto order = manifest_order(scene);
  const gwipc_surface_upsert* cursor = nullptr;
  for (const auto& [id, surface] : scene.surfaces) {
    static_cast<void>(id);
    if (!cursor_surface(surface)) continue;
    if (cursor) {
      error = "scene manifest has more than one cursor surface";
      return false;
    }
    cursor = &surface;
  }
  std::uint64_t hash = kFnvOffset;
  constexpr std::string_view tag = "glasswyrm-scene-v1";
  hash_bytes(hash,
             {reinterpret_cast<const std::uint8_t *>(tag.data()), tag.size()});
  if (!hash_contract(hash, *scene.output, gwipc_contract_encode_output_upsert,
                     error))
    return false;
  for (const auto id : order) {
    const auto policy = scene.surface_policies.find(id);
    if (policy == scene.surface_policies.end()) {
      error = "scene manifest surface lacks policy metadata";
      return false;
    }
    if (!hash_contract(hash, scene.surfaces.at(id),
                       gwipc_contract_encode_surface_upsert, error) ||
        !hash_contract(hash, policy->second,
                       gwipc_contract_encode_surface_policy_upsert, error))
      return false;
  }
  if (cursor &&
      !hash_contract(hash, *cursor, gwipc_contract_encode_surface_upsert,
                     error))
    return false;

  std::ostringstream output;
  output << "{\"commit_id\":" << commit_id << ",\"generation\":" << generation
         << ",\"output_id\":" << scene.output->output_id
         << ",\"scene_hash\":\"";
  output.setf(std::ios::hex, std::ios::basefield);
  output.width(16);
  output.fill('0');
  output << hash;
  output.setf(std::ios::dec, std::ios::basefield);
  output << "\",\"surface_count\":" << order.size() << ",\"surfaces\":[";
  for (std::size_t index = 0; index < order.size(); ++index) {
    const auto id = order[index];
    const auto &surface = scene.surfaces.at(id);
    const auto &policy = scene.surface_policies.at(id);
    if (index != 0)
      output << ',';
    output << "{\"surface_id\":" << surface.surface_id
           << ",\"x11_window_id\":" << surface.x11_window_id
           << ",\"workspace_id\":" << policy.workspace_id
           << ",\"x\":" << surface.logical_x << ",\"y\":" << surface.logical_y
           << ",\"width\":" << surface.logical_width
           << ",\"height\":" << surface.logical_height
           << ",\"stacking\":" << surface.stacking
           << ",\"visible\":" << boolean(surface.visible)
           << ",\"metadata_only\":"
           << boolean(surface.presentation_flags ==
                      GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
           << ",\"focused\":" << boolean(policy.focused)
           << ",\"managed\":" << boolean(policy.managed)
           << ",\"decoration_eligible\":" << boolean(policy.decoration_eligible)
           << ",\"override_redirect\":" << boolean(policy.override_redirect)
           << ",\"applied_state\":\"" << applied(policy.applied_state)
           << "\",\"fullscreen_eligible\":\"" << tri(policy.fullscreen_eligible)
           << "\",\"direct_scanout_eligible\":\""
           << tri(policy.direct_scanout_eligible) << "\"}";
  }
  output << ']';
  if (cursor) {
    output << ",\"cursor_surface\":{\"surface_id\":" << cursor->surface_id
           << ",\"output_id\":" << cursor->output_id
           << ",\"x\":" << cursor->logical_x
           << ",\"y\":" << cursor->logical_y
           << ",\"width\":" << cursor->logical_width
           << ",\"height\":" << cursor->logical_height
           << ",\"visible\":" << boolean(cursor->visible)
           << ",\"format\":\"ARGB8888Premultiplied\"}";
  }
  output << "}\n";
  if (!output.good()) {
    error = "scene manifest serialization failed";
    return false;
  }
  result.hash = hash;
  result.surface_count = static_cast<std::uint32_t>(order.size());
  result.cursor_count = cursor ? 1U : 0U;
  json = output.str();
  return true;
}

bool SceneManifest::append(const std::uint64_t commit_id,
                           const std::uint64_t generation, const Scene &scene,
                           SceneManifestResult &result,
                           std::string &error) const {
  PreparedSceneManifest prepared;
  if (!prepare(commit_id, generation, scene, prepared, error)) return false;
  if (!publish(prepared, error)) return false;
  result = prepared.result;
  return true;
}

bool SceneManifest::prepare(const std::uint64_t commit_id,
                            const std::uint64_t generation, const Scene &scene,
                            PreparedSceneManifest &prepared,
                            std::string &error) {
  PreparedSceneManifest replacement;
  if (!describe(commit_id, generation, scene, replacement.result,
                replacement.json, error))
    return false;
  replacement.active = true;
  prepared = std::move(replacement);
  return true;
}

bool SceneManifest::publish(PreparedSceneManifest &prepared,
                            std::string &error) const {
  if (!prepared.active) {
    error = "scene manifest record is not prepared";
    return false;
  }
  std::error_code filesystem_error;
  auto parent = path_.parent_path();
  if (parent.empty())
    parent = ".";
  const auto parent_status =
      std::filesystem::symlink_status(parent, filesystem_error);
  if (filesystem_error &&
      filesystem_error != std::errc::no_such_file_or_directory) {
    error = filesystem_error.message();
    return false;
  }
  if (!std::filesystem::exists(parent_status) &&
      !std::filesystem::create_directories(parent, filesystem_error)) {
    if (filesystem_error)
      error = filesystem_error.message();
    else
      error = "scene manifest parent was not created";
    return false;
  }
  const auto checked_parent =
      std::filesystem::symlink_status(parent, filesystem_error);
  if (filesystem_error || !std::filesystem::is_directory(checked_parent) ||
      std::filesystem::is_symlink(checked_parent)) {
    error = "scene manifest parent must be a real directory";
    return false;
  }
  const int fd =
      io_->open(path_.c_str(),
                O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    const int error_number = errno;
    error = describe_failure(
        errno_failure("scene manifest open failed", error_number));
    return false;
  }

  PublishFailure primary;
  bool failed = false;
  std::vector<std::string> secondary;
  auto fail_errno = [&](std::string stage, const int error_number) {
    if (failed)
      return;
    primary = errno_failure(std::move(stage), error_number);
    failed = true;
  };
  auto fail_detail = [&](std::string stage, std::string detail) {
    if (failed)
      return;
    primary = {std::move(stage), 0, std::move(detail)};
    failed = true;
  };

  struct stat status{};
  if (io_->stat(fd, &status) != 0) {
    const int error_number = errno;
    fail_errno("scene manifest stat failed", error_number);
  } else if (!S_ISREG(status.st_mode)) {
    fail_detail("scene manifest target rejected",
                "target is not a regular file");
  } else if (status.st_size < 0) {
    fail_detail("scene manifest target rejected", "target has a negative size");
  }

  bool locked = false;
  if (!failed) {
    if (io_->lock(fd, LOCK_EX) != 0) {
      const int error_number = errno;
      fail_errno("scene manifest lock failed", error_number);
    } else {
      locked = true;
    }
  }

  off_t original_size = status.st_size;
  off_t expected_size = status.st_size;
  bool already_published = false;
  if (!failed && prepared.publication_uncertain) {
    const auto saved_original = prepared.uncertain_original_size;
    const auto saved_expected = prepared.uncertain_expected_size;
    if (static_cast<std::uint64_t>(status.st_size) == saved_original) {
      prepared.publication_uncertain = false;
      prepared.uncertain_original_size = 0;
      prepared.uncertain_expected_size = 0;
    } else if (static_cast<std::uint64_t>(status.st_size) == saved_expected) {
      int read_error = 0;
      if (tail_matches(*io_, fd, status.st_size, prepared.json, read_error)) {
        already_published = true;
      } else if (read_error != 0) {
        fail_errno("scene manifest uncertain publication read failed",
                   read_error);
      } else {
        fail_detail("scene manifest uncertain publication rejected",
                    "target tail does not match the prepared record");
      }
    } else {
      fail_detail(
          "scene manifest uncertain publication rejected",
          "target size does not match the original or completed append");
    }
  }

  if (!failed && !already_published) {
    const auto maximum_size = std::numeric_limits<off_t>::max();
    if (prepared.json.size() >
        static_cast<std::uint64_t>(maximum_size - original_size)) {
      fail_errno("scene manifest append failed", EFBIG);
    } else {
      expected_size = original_size + static_cast<off_t>(prepared.json.size());
    }
  }

  bool append_changed = false;
  if (!failed && !already_published) {
    const auto written = write_all(*io_, fd, prepared.json);
    append_changed = written.changed;
    if (!written.complete) {
      fail_errno("scene manifest append write failed", written.error_number);
    } else if (io_->synchronize(fd) != 0) {
      const int error_number = errno;
      append_changed = !prepared.json.empty();
      fail_errno("scene manifest append synchronization failed", error_number);
    }
  }

  auto remember_uncertain_publication = [&] {
    prepared.publication_uncertain = true;
    prepared.uncertain_original_size =
        static_cast<std::uint64_t>(original_size);
    prepared.uncertain_expected_size =
        static_cast<std::uint64_t>(expected_size);
  };
  auto rollback = [&] {
    if (!append_changed)
      return true;
    remember_uncertain_publication();
    if (io_->truncate(fd, original_size) != 0) {
      const int error_number = errno;
      add_secondary(secondary, "scene manifest rollback truncate failed",
                    error_number);
      return false;
    }
    if (io_->synchronize(fd) != 0) {
      const int error_number = errno;
      add_secondary(secondary, "scene manifest rollback synchronization failed",
                    error_number);
      return false;
    }
    prepared.publication_uncertain = false;
    prepared.uncertain_original_size = 0;
    prepared.uncertain_expected_size = 0;
    return true;
  };

  if (failed && append_changed)
    static_cast<void>(rollback());

  if (locked) {
    if (io_->lock(fd, LOCK_UN) != 0) {
      const int error_number = errno;
      if (!failed) {
        fail_errno("scene manifest unlock failed", error_number);
        static_cast<void>(rollback());
      } else {
        add_secondary(secondary, "scene manifest unlock failed", error_number);
      }
      if (io_->lock(fd, LOCK_UN) != 0) {
        const int retry_error = errno;
        add_secondary(secondary, "scene manifest unlock retry failed",
                      retry_error);
      } else {
        locked = false;
      }
    } else {
      locked = false;
    }
  }

  if (io_->close(fd) != 0) {
    const int error_number = errno;
    if (!failed) {
      fail_errno("scene manifest close failed", error_number);
      if (append_changed || already_published)
        remember_uncertain_publication();
    } else {
      add_secondary(secondary, "scene manifest close failed", error_number);
    }
  }

  if (failed) {
    error = combined_error(primary, secondary);
    return false;
  }
  prepared.active = false;
  prepared.publication_uncertain = false;
  prepared.uncertain_original_size = 0;
  prepared.uncertain_expected_size = 0;
  error.clear();
  return true;
}

void SceneManifest::abort(PreparedSceneManifest &prepared) noexcept {
  prepared.active = false;
  prepared.publication_uncertain = false;
  prepared.uncertain_original_size = 0;
  prepared.uncertain_expected_size = 0;
  prepared.json.clear();
}

} // namespace gw::compositor
