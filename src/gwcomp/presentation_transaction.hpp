#pragma once

#include <glasswyrm/ipc.h>

#include "backends/output/software_frame.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"
#include "gwcomp/compositor.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gw::compositor {

struct Scene;

class PresentationTransaction final {
public:
  [[nodiscard]] static PresentedFrame commit(Compositor& compositor,
                                             const gwipc_frame_commit& value,
                                             std::string& error);
  [[nodiscard]] static PresentationCompletion service(
      Compositor& compositor, short revents, std::string& error);
  [[nodiscard]] static int timeout_ms(const Compositor& compositor);
  static void abort(Compositor& compositor,
                    std::string_view reason = {}) noexcept;

private:
  using AttachmentMap = std::map<std::uint64_t, std::uint64_t>;
  using ReleaseMap =
      std::map<std::uint64_t, gwipc_buffer_release_reason>;

  struct ValidatedCommit {
    SceneModel candidate;
    CommitResult result;
    std::vector<std::uint64_t> content_changed;
    bool metadata_only_peer{};
    bool protocol_server{};
  };

  struct PreparedOutputFrame {
    glasswyrm::output::SoftwareFrame frame;
    std::optional<glasswyrm::output::SoftwareFrameSet> frame_set;
    std::vector<Rectangle> damage;
    std::uint64_t canonical_hash{};
    ReleaseMap releases;
    std::optional<PreparedSceneManifest> manifest;
  };

  PresentationTransaction(SceneModel candidate, AttachmentMap attachments,
                          ReleaseMap releases,
                          glasswyrm::output::SoftwareFrame frame,
                          std::optional<glasswyrm::output::SoftwareFrameSet>
                              frame_set,
                          gwipc_frame_commit commit, PresentedFrame presented,
                          std::optional<PreparedSceneManifest> manifest,
                          std::uint64_t token,
                          std::chrono::steady_clock::time_point deadline);
  [[nodiscard]] static ReleaseMap calculate_retired_buffers(
      const Compositor& compositor, const Scene& staged);
  static void release_retired_buffers(Compositor& compositor,
                                      const Scene& staged);
  [[nodiscard]] static std::optional<ValidatedCommit>
  validate_scene(Compositor& compositor, const gwipc_frame_commit& value,
                 PresentedFrame& presented, std::string& error);
  [[nodiscard]] static bool validate_scene_surface(
      const Compositor& compositor, const Scene& staged,
      std::uint64_t surface_id, const gwipc_surface_upsert& surface,
      const ValidatedCommit& validated, std::size_t& policy_surface_count,
      PresentedFrame& presented, std::string& error);
  [[nodiscard]] static bool
  validate_surface_attachment(const Compositor& compositor,
                              std::uint64_t surface_id,
                              const gwipc_surface_upsert& surface,
                              PresentedFrame& presented, std::string& error);
  [[nodiscard]] static bool
  validate_attachments(const Compositor& compositor, const Scene& staged,
                       const ValidatedCommit& validated,
                       PresentedFrame& presented, std::string& error);
  [[nodiscard]] static std::optional<PreparedOutputFrame>
  prepare_output_frame(Compositor& compositor, ValidatedCommit& validated,
                       const gwipc_frame_commit& value,
                       PresentedFrame& presented, std::string& error);
  [[nodiscard]] static std::optional<PreparedOutputFrame>
  prepare_output_frame_set(Compositor& compositor, ValidatedCommit& validated,
                           const gwipc_frame_commit& value,
                           PresentedFrame& presented, std::string& error);
  [[nodiscard]] static PresentedFrame
  stage_presentation(Compositor& compositor, ValidatedCommit&& validated,
                     PreparedOutputFrame&& prepared,
                     const gwipc_frame_commit& value, PresentedFrame presented,
                     std::string& error);
  [[nodiscard]] static PresentedFrame
  promote_disabled_output(Compositor& compositor, ValidatedCommit&& validated,
                          const gwipc_frame_commit& value,
                          PresentedFrame presented);
  [[nodiscard]] static PresentedFrame
  promote_metadata_only(Compositor& compositor, ValidatedCommit&& validated,
                        const gwipc_frame_commit& value,
                        PresentedFrame presented, std::string& error);
  [[nodiscard]] PresentedFrame promote(Compositor& compositor,
                                       std::uint64_t visible_hash);
  [[nodiscard]] bool publish_manifest(Compositor& compositor,
                                      std::string& error);

  SceneModel candidate_;
  AttachmentMap attachments_;
  ReleaseMap releases_;
  glasswyrm::output::SoftwareFrame frame_;
  std::optional<glasswyrm::output::SoftwareFrameSet> frame_set_;
  gwipc_frame_commit commit_{};
  PresentedFrame presented_;
  std::optional<PreparedSceneManifest> manifest_;
  std::uint64_t token_{};
  std::chrono::steady_clock::time_point deadline_;
};

} // namespace gw::compositor
