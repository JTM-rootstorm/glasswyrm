#pragma once

#include "compositor/damage_region.hpp"

#include <glasswyrm/ipc/contracts.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gw::compositor {

struct Scene {
  std::optional<gwipc_output_upsert> output;
  std::map<std::uint64_t, gwipc_surface_upsert> surfaces;
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
  [[nodiscard]] bool begin_complete_snapshot();
  [[nodiscard]] bool end_complete_snapshot();
  void abort_complete_snapshot();

  [[nodiscard]] bool apply(const gwipc_output_upsert& output);
  [[nodiscard]] bool apply(const gwipc_output_remove& output);
  [[nodiscard]] bool apply(const gwipc_surface_upsert& surface);
  [[nodiscard]] bool apply(const gwipc_surface_remove& surface);
  [[nodiscard]] bool apply(const gwipc_surface_damage& damage);

  [[nodiscard]] CommitResult commit(const gwipc_frame_commit& commit);
  void disconnect();

  [[nodiscard]] const Scene& committed() const noexcept { return committed_; }
  [[nodiscard]] const Scene& pending() const noexcept { return pending_; }
  [[nodiscard]] bool initial_snapshot_received() const noexcept {
    return initial_snapshot_received_;
  }
  [[nodiscard]] bool snapshot_active() const noexcept { return snapshot_active_; }

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
  bool snapshot_active_{};
  bool initial_snapshot_received_{};
};

} // namespace gw::compositor
