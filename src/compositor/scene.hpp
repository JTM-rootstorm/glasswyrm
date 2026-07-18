#pragma once

#include "compositor/damage_region.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gw::compositor {

enum class SceneProfile {
  Historical,
  OutputModel,
};

struct SurfaceOutputMembership {
  std::uint64_t primary_output_id{};
  std::vector<std::uint64_t> output_ids;
  std::uint32_t preferred_scale_numerator{};
  std::uint32_t preferred_scale_denominator{};
  std::uint32_t client_buffer_scale{};
  gwipc_surface_scale_mode scale_mode{GWIPC_SURFACE_SCALE_LEGACY};
  std::uint64_t layout_generation{};
  std::uint32_t flags{};

  friend bool operator==(const SurfaceOutputMembership &,
                         const SurfaceOutputMembership &) = default;
};

struct Scene {
  // Retained as the historical-profile view until all existing renderer and
  // presenter paths are taught to consume output sets.
  std::optional<gwipc_output_upsert> output;
  std::map<std::uint64_t, gwipc_output_upsert> outputs;
  std::map<std::uint64_t, gwipc_surface_upsert> surfaces;
  std::map<std::uint64_t, gwipc_surface_policy_upsert> surface_policies;
  std::map<std::uint64_t, SurfaceOutputMembership> surface_outputs;
  std::uint64_t primary_output_id{};
  std::uint64_t configuration_generation{};
};

struct CommitResult {
  gwipc_frame_result result{GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA};
  std::uint64_t presented_generation{};
  std::vector<Rectangle> damage;

  [[nodiscard]] bool accepted() const noexcept {
    return result == GWIPC_FRAME_ACCEPTED || result == GWIPC_FRAME_DROPPED;
  }
};

class SceneModel {
public:
  explicit SceneModel(SceneProfile profile = SceneProfile::Historical)
      : profile_(profile) {}

  [[nodiscard]] bool begin_complete_snapshot();
  [[nodiscard]] bool
  begin_complete_snapshot(std::uint64_t primary_output_id,
                          std::uint64_t configuration_generation);
  [[nodiscard]] bool end_complete_snapshot();
  void abort_complete_snapshot();

  [[nodiscard]] bool apply(const gwipc_output_upsert &output);
  [[nodiscard]] bool apply(const gwipc_output_remove &output);
  [[nodiscard]] bool apply(const gwipc_surface_upsert &surface);
  [[nodiscard]] bool apply(const gwipc_surface_output_state &state);
  [[nodiscard]] bool apply(const gwipc_surface_policy_upsert &policy);
  [[nodiscard]] bool apply(const gwipc_surface_remove &surface);
  [[nodiscard]] bool apply(const gwipc_surface_damage &damage);

  [[nodiscard]] CommitResult commit(const gwipc_frame_commit &commit);
  void disconnect();

  [[nodiscard]] const Scene &committed() const noexcept { return committed_; }
  [[nodiscard]] const Scene &pending() const noexcept { return pending_; }
  [[nodiscard]] bool initial_snapshot_received() const noexcept {
    return initial_snapshot_received_;
  }
  [[nodiscard]] bool snapshot_active() const noexcept {
    return snapshot_active_;
  }
  [[nodiscard]] SceneProfile profile() const noexcept { return profile_; }
  [[nodiscard]] std::vector<std::uint64_t> pending_damage_surface_ids() const;

  [[nodiscard]] std::vector<std::uint64_t> stacking_order() const;

private:
  struct PendingDamage {
    std::uint64_t surface_id{};
    std::vector<gwipc_damage_rectangle> rectangles;
  };
  [[nodiscard]] bool mutations_allowed() const noexcept;

  Scene committed_;
  Scene pending_;
  std::optional<Scene> pre_snapshot_pending_;
  std::vector<PendingDamage> explicit_damage_;
  std::vector<PendingDamage> pre_snapshot_damage_;
  std::uint64_t presented_generation_{};
  SceneProfile profile_{SceneProfile::Historical};
  bool snapshot_active_{};
  bool initial_snapshot_received_{};
};

} // namespace gw::compositor
