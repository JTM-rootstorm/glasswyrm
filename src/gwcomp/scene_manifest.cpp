#include "gwcomp/scene_manifest.hpp"

#include <glasswyrm/ipc.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
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

std::vector<std::uint64_t> manifest_order(const Scene &scene) {
  std::vector<std::uint64_t> visible;
  std::vector<std::uint64_t> hidden;
  for (const auto &[id, surface] : scene.surfaces)
    (surface.visible ? visible : hidden).push_back(id);
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

bool write_all(int fd, std::string_view bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count =
        ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count > 0)
      offset += static_cast<std::size_t>(count);
    else if (count < 0 && errno == EINTR)
      continue;
    else
      return false;
  }
  return true;
}

} // namespace

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
  output << "]}\n";
  if (!output.good()) {
    error = "scene manifest serialization failed";
    return false;
  }
  result.hash = hash;
  result.surface_count = static_cast<std::uint32_t>(order.size());
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
      ::open(path_.c_str(),
             O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    error = std::string("scene manifest open failed: ") + std::strerror(errno);
    return false;
  }
  struct stat status{};
  bool ok = ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
            ::flock(fd, LOCK_EX) == 0;
  const off_t original_size = ok ? status.st_size : -1;
  if (ok)
    ok = write_all(fd, prepared.json) && ::fdatasync(fd) == 0;
  if (!ok && original_size >= 0) {
    (void)::ftruncate(fd, original_size);
    (void)::fdatasync(fd);
  }
  const int saved_errno = errno;
  if (original_size >= 0)
    (void)::flock(fd, LOCK_UN);
  (void)::close(fd);
  if (!ok) {
    error = std::string("scene manifest append failed: ") +
            std::strerror(saved_errno);
    return false;
  }
  prepared.active = false;
  error.clear();
  return true;
}

void SceneManifest::abort(PreparedSceneManifest &prepared) noexcept {
  prepared.active = false;
  prepared.json.clear();
}

} // namespace gw::compositor
