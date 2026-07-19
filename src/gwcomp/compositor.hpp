#pragma once

#include "backends/output/presentation_backend.hpp"
#include "backends/output/software_frame.hpp"
#include "backends/output/software_frame_set.hpp"
#include "config.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"
#include "gwcomp/scene_manifest.hpp"
#include "gwcomp/vrr_response_batch.hpp"
#include "render/scene_renderer.hpp"
#include "render/output_scene_renderer.hpp"

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
#include <vector>

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
  std::vector<VrrResponseMessage> vrr_response;
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
      PresentationTiming timing = {},
      std::unique_ptr<gw::render::SceneRenderer> renderer = nullptr,
      std::unique_ptr<gw::render::OutputSceneRenderer> output_renderer =
          nullptr);
  ~Compositor();

  void set_peer_profile(PeerProfile profile) noexcept { profile_ = profile; }
  void set_cpu_buffer_synchronization(const bool enabled) noexcept {
    cpu_buffer_synchronization_ = enabled;
  }
  void set_vrr_contract_enabled(const bool enabled) noexcept {
    vrr_contract_enabled_ = enabled;
  }
  [[nodiscard]] bool configure_scene_profile(
      SceneProfile profile, std::uint64_t primary_output_id = 0,
      std::uint64_t output_layout_generation = 0) noexcept;
  [[nodiscard]] PeerProfile peer_profile() const noexcept { return profile_; }
  [[nodiscard]] bool begin_snapshot(std::uint64_t generation = 0);
  [[nodiscard]] bool end_snapshot();
  void abort_snapshot();
  [[nodiscard]] bool apply(const gwipc_output_upsert& value);
  [[nodiscard]] bool apply(const gwipc_output_remove& value);
  [[nodiscard]] bool apply(const gwipc_surface_upsert& value);
  [[nodiscard]] bool apply(const gwipc_surface_output_state& value);
  [[nodiscard]] bool apply(const gwipc_surface_policy_upsert& value);
  [[nodiscard]] bool apply(const gwipc_output_vrr_policy_upsert& value);
  [[nodiscard]] bool apply(const gwipc_surface_vrr_state& value);
  [[nodiscard]] bool apply(const gwipc_surface_remove& value);
  [[nodiscard]] bool apply(const gwipc_surface_damage& value);
  [[nodiscard]] bool attach(const gwipc_buffer_attach& value, int fd,
                            int synchronization_fd, std::string& error);
  [[nodiscard]] bool attach(const gwipc_buffer_attach& value, int fd,
                            std::string& error) {
    return attach(value, fd, -1, error);
  }
  [[nodiscard]] bool detach(const gwipc_buffer_detach& value);
  [[nodiscard]] PresentedFrame commit(const gwipc_frame_commit& value,
                                      std::string& error);
  [[nodiscard]] bool presentation_pending() const noexcept;
  [[nodiscard]] int presentation_poll_fd() const noexcept;
  [[nodiscard]] short presentation_poll_events() const noexcept;
  [[nodiscard]] int presentation_timeout_ms() const;
  [[nodiscard]] PresentationCompletion service_presentation(
      short revents, std::string& error);
  [[nodiscard]] bool suspend_presentation(std::string& error);
  [[nodiscard]] bool resume_presentation(std::string& error);
  [[nodiscard]] bool shutdown_presentation(std::string& error) noexcept;
  [[nodiscard]] bool presentation_suspended() const noexcept {
    return presentation_suspended_;
  }
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

  struct PendingBufferReadiness {
    gwipc_frame_commit commit{};
    std::vector<Mapping> mappings;
    std::size_t next{};
    PresentationTiming::Clock::time_point deadline;
  };

  SceneModel scene_;
  MappingMap mappings_;
  AttachmentMap pending_attachments_;
  AttachmentMap committed_attachments_;
  AttachmentMap pre_snapshot_attachments_;
  glasswyrm::output::SoftwareFrame output_;
  std::optional<glasswyrm::output::SoftwareFrameSet> output_set_;
  std::unique_ptr<gw::render::SceneRenderer> renderer_;
  std::unique_ptr<gw::render::OutputSceneRenderer> output_renderer_;
  std::unique_ptr<glasswyrm::output::PresentationBackend> presenter_;
  std::unique_ptr<PresentationTransaction> pending_presentation_;
  std::optional<PendingBufferReadiness> pending_buffer_readiness_;
  PresentationTiming timing_;
  bool presentation_suspended_{};
  bool presentation_shutdown_{};
  std::optional<SceneManifest> scene_manifest_;
  std::map<std::uint64_t, gwipc_buffer_release_reason> releases_;
  CommittedVrrState committed_vrr_;
  std::uint64_t frame_ordinal_{};
  std::uint64_t last_commit_id_{};
  std::uint64_t last_generation_{};
  bool snapshot_active_{};
  bool snapshot_invalid_{};
  std::set<std::uint64_t> snapshot_surface_ids_;
  std::set<std::uint64_t> snapshot_policy_ids_;
  std::set<std::uint64_t> snapshot_output_ids_;
  std::set<std::uint64_t> snapshot_surface_output_ids_;
  std::uint64_t primary_output_id_{};
  std::uint64_t output_layout_generation_{};
  PeerProfile profile_{PeerProfile::M4TestProducer};
  bool cpu_buffer_synchronization_{};
  bool vrr_contract_enabled_{};
};

} // namespace gw::compositor
