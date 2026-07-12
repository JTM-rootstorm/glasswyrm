#pragma once

#include "compositor/damage_region.hpp"
#include "ipc/wire/compositor_contract.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gw::compositor {

struct Scene {
  std::optional<ipc::wire::OutputUpsert> output;
  std::map<std::uint64_t, ipc::wire::SurfaceUpsert> surfaces;
};

struct CommitResult {
  ipc::wire::FrameResult result{ipc::wire::FrameResult::RejectedIncompleteMetadata};
  std::uint64_t presented_generation{};
  std::vector<Rectangle> damage;

  [[nodiscard]] bool accepted() const noexcept {
    return result == ipc::wire::FrameResult::Accepted ||
           result == ipc::wire::FrameResult::Dropped;
  }
};

class SceneModel {
public:
  [[nodiscard]] bool begin_complete_snapshot();
  [[nodiscard]] bool end_complete_snapshot();
  void abort_complete_snapshot();

  [[nodiscard]] bool apply(const ipc::wire::OutputUpsert& output);
  [[nodiscard]] bool apply(const ipc::wire::OutputRemove& output);
  [[nodiscard]] bool apply(const ipc::wire::SurfaceUpsert& surface);
  [[nodiscard]] bool apply(const ipc::wire::SurfaceRemove& surface);
  [[nodiscard]] bool apply(const ipc::wire::SurfaceDamage& damage);

  [[nodiscard]] CommitResult commit(const ipc::wire::FrameCommit& commit);
  void disconnect();

  [[nodiscard]] const Scene& committed() const noexcept { return committed_; }
  [[nodiscard]] const Scene& pending() const noexcept { return pending_; }
  [[nodiscard]] bool initial_snapshot_received() const noexcept {
    return initial_snapshot_received_;
  }
  [[nodiscard]] bool snapshot_active() const noexcept { return snapshot_active_; }

  [[nodiscard]] std::vector<std::uint64_t> stacking_order() const;

private:
  [[nodiscard]] bool mutations_allowed() const noexcept;

  Scene committed_;
  Scene pending_;
  std::optional<Scene> pre_snapshot_pending_;
  std::vector<ipc::wire::SurfaceDamage> explicit_damage_;
  std::vector<ipc::wire::SurfaceDamage> pre_snapshot_damage_;
  std::uint64_t presented_generation_{};
  bool snapshot_active_{};
  bool initial_snapshot_received_{};
};

} // namespace gw::compositor
