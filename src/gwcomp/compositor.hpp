#pragma once

#include "backends/output/presentation_backend.hpp"
#include "backends/output/software_frame.hpp"
#include "config.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"
#include "gwcomp/scene_manifest.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace gw::compositor {

enum class PeerProfile {
  M4TestProducer,
  M6MetadataProtocolServer,
  M7BufferedProtocolServer,
};

[[nodiscard]] std::optional<PeerProfile>
select_peer_profile(gwipc_role role, std::uint64_t capabilities) noexcept;

struct PresentedFrame {
  enum class Disposition { Complete, Pending, Rejected, Fatal };

  Disposition disposition{Disposition::Rejected};
  gwipc_frame_result result{GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA};
  std::uint64_t generation{};
  std::uint64_t ordinal{};
  std::uint64_t hash{};
};

struct PresentationTiming {
  using Clock = std::chrono::steady_clock;
  std::chrono::milliseconds timeout{2000};
  std::function<Clock::time_point()> now{[] { return Clock::now(); }};
};

enum class PresentationCompletionKind { None, Complete, Fatal };

struct PresentationCompletion {
  PresentationCompletionKind kind{PresentationCompletionKind::None};
  gwipc_frame_commit commit{};
  PresentedFrame frame;
};

class PresentationTransaction;

class Compositor final {
public:
#if GW_HAS_HEADLESS_BACKEND
  explicit Compositor(
      std::filesystem::path dump_directory,
      std::optional<std::filesystem::path> scene_manifest = std::nullopt);
#endif
  explicit Compositor(
      std::unique_ptr<glasswyrm::output::PresentationBackend> presenter,
      std::optional<std::filesystem::path> scene_manifest = std::nullopt,
      PresentationTiming timing = {});
  ~Compositor();

  void set_peer_profile(PeerProfile profile) noexcept { profile_ = profile; }
  [[nodiscard]] PeerProfile peer_profile() const noexcept { return profile_; }
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
                                      std::string& error);
  [[nodiscard]] bool presentation_pending() const noexcept;
  [[nodiscard]] int presentation_poll_fd() const noexcept;
  [[nodiscard]] short presentation_poll_events() const noexcept;
  [[nodiscard]] int presentation_timeout_ms() const;
  [[nodiscard]] PresentationCompletion service_presentation(
      short revents, std::string& error);
  void disconnect();

  [[nodiscard]] const std::map<std::uint64_t, gwipc_buffer_release_reason>&
  releases() const noexcept { return releases_; }
  void clear_releases() noexcept { releases_.clear(); }
  [[nodiscard]] std::uint64_t accepted_frames() const noexcept { return frame_ordinal_; }

private:
  friend class PresentationTransaction;

  using Mapping = std::shared_ptr<BufferMapping>;
  using MappingMap = std::map<std::uint64_t, Mapping>;
  using AttachmentMap = std::map<std::uint64_t, std::uint64_t>;

  SceneModel scene_;
  MappingMap mappings_;
  AttachmentMap pending_attachments_;
  AttachmentMap committed_attachments_;
  AttachmentMap pre_snapshot_attachments_;
  glasswyrm::output::SoftwareFrame output_;
  std::unique_ptr<glasswyrm::output::PresentationBackend> presenter_;
  std::unique_ptr<PresentationTransaction> pending_presentation_;
  PresentationTiming timing_;
  std::optional<SceneManifest> scene_manifest_;
  std::map<std::uint64_t, gwipc_buffer_release_reason> releases_;
  std::uint64_t frame_ordinal_{};
  std::uint64_t last_commit_id_{};
  std::uint64_t last_generation_{};
  bool snapshot_active_{};
  bool snapshot_invalid_{};
  std::set<std::uint64_t> snapshot_surface_ids_;
  std::set<std::uint64_t> snapshot_policy_ids_;
  PeerProfile profile_{PeerProfile::M4TestProducer};
};

} // namespace gw::compositor
