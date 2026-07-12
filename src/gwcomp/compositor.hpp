#pragma once

#include "backends/headless/frame_dump.hpp"
#include "backends/headless/output.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"
#include "gwcomp/scene_manifest.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace gw::compositor {

struct PresentedFrame {
  gwipc_frame_result result{GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA};
  std::uint64_t generation{};
  std::uint64_t ordinal{};
  std::uint64_t hash{};
};

class Compositor final {
public:
  explicit Compositor(
      std::filesystem::path dump_directory,
      std::optional<std::filesystem::path> scene_manifest = std::nullopt);

  [[nodiscard]] bool begin_snapshot();
  [[nodiscard]] bool end_snapshot();
  void abort_snapshot();
  [[nodiscard]] bool apply(const gwipc_output_upsert& value);
  [[nodiscard]] bool apply(const gwipc_output_remove& value);
  [[nodiscard]] bool apply(const gwipc_surface_upsert& value);
  [[nodiscard]] bool apply(const gwipc_surface_policy_upsert& value);
  [[nodiscard]] bool apply(const gwipc_surface_remove& value);
  [[nodiscard]] bool apply(const gwipc_surface_damage& value);
  [[nodiscard]] bool attach(const gwipc_buffer_attach& value, int fd,
                            std::string& error);
  [[nodiscard]] bool detach(const gwipc_buffer_detach& value);
  [[nodiscard]] PresentedFrame commit(const gwipc_frame_commit& value,
                                      std::string& error,
                                      bool metadata_only_peer = false);
  void disconnect();

  [[nodiscard]] const std::map<std::uint64_t, gwipc_buffer_release_reason>&
  releases() const noexcept { return releases_; }
  void clear_releases() noexcept { releases_.clear(); }
  [[nodiscard]] std::uint64_t accepted_frames() const noexcept { return frame_ordinal_; }

private:
  using Mapping = std::shared_ptr<BufferMapping>;
  using MappingMap = std::map<std::uint64_t, Mapping>;
  using AttachmentMap = std::map<std::uint64_t, std::uint64_t>;

  SceneModel scene_;
  MappingMap mappings_;
  AttachmentMap pending_attachments_;
  AttachmentMap committed_attachments_;
  AttachmentMap pre_snapshot_attachments_;
  glasswyrm::headless::Output output_;
  glasswyrm::headless::FrameDumper dumper_;
  std::optional<SceneManifest> scene_manifest_;
  std::map<std::uint64_t, gwipc_buffer_release_reason> releases_;
  std::uint64_t frame_ordinal_{};
  std::uint64_t last_commit_id_{};
  std::uint64_t last_generation_{};
  bool snapshot_active_{};
  bool snapshot_invalid_{};
  std::set<std::uint64_t> snapshot_surface_ids_;
  std::set<std::uint64_t> snapshot_policy_ids_;
};

} // namespace gw::compositor
