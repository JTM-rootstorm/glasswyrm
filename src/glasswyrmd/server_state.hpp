#pragma once

#include "glasswyrmd/atom_table.hpp"
#include "glasswyrmd/resource_table.hpp"
#include "glasswyrmd/lifecycle_snapshot.hpp"

#include <limits>
#include <optional>

namespace glasswyrm::server {

class LifecycleSerialSource {
 public:
  explicit LifecycleSerialSource(std::uint64_t next = 1) : next_(next) {}
  [[nodiscard]] std::optional<std::uint64_t> take() noexcept {
    if (exhausted_ || next_ == 0) { exhausted_ = true; return std::nullopt; }
    const auto result = next_;
    if (next_ == std::numeric_limits<std::uint64_t>::max()) exhausted_ = true;
    else ++next_;
    return result;
  }
  [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }
 private:
  std::uint64_t next_{1};
  bool exhausted_{false};
};

class ServerState {
 public:
  explicit ServerState(ScreenModel screen = kScreenModel)
      : screen_(screen), resources_(screen) {}

  [[nodiscard]] const ScreenModel& screen() const noexcept { return screen_; }
  [[nodiscard]] ResourceTable& resources() noexcept { return resources_; }
  [[nodiscard]] const ResourceTable& resources() const noexcept {
    return resources_;
  }
  [[nodiscard]] AtomTable& atoms() noexcept { return atoms_; }
  [[nodiscard]] const AtomTable& atoms() const noexcept { return atoms_; }
  [[nodiscard]] std::optional<std::uint64_t> next_lifecycle_serial() noexcept {
    return lifecycle_serials_.take();
  }
  [[nodiscard]] std::uint32_t focused_window() const noexcept {
    return focused_window_;
  }
  [[nodiscard]] LifecycleSnapshot lifecycle_snapshot() const {
    return build_lifecycle_snapshot(resources_, focused_window_);
  }
  [[nodiscard]] bool apply_policy(std::span<const AppliedPolicyWindow> policy) {
    std::vector<std::pair<std::uint32_t, std::uint64_t>> placements;
    for (const auto& item : policy) {
      const auto* window = resources_.find_window(item.xid);
      if (window && window->map_requested && window->geometry_serial == 0) {
        const auto serial = lifecycle_serials_.take();
        if (!serial) return false;
        placements.emplace_back(item.xid, *serial);
      }
    }
    if (!apply_policy_state(resources_, policy, focused_window_)) return false;
    for (const auto [xid, serial] : placements)
      resources_.find_window(xid)->geometry_serial = serial;
    return true;
  }

  [[nodiscard]] CleanupResult cleanup_client(ClientId owner) {
    return resources_.cleanup_client(owner);
  }
  [[nodiscard]] bool invariants_hold() const noexcept {
    return resources_.invariants_hold();
  }

 private:
  ScreenModel screen_;
  ResourceTable resources_;
  AtomTable atoms_;
  LifecycleSerialSource lifecycle_serials_;
  std::uint32_t focused_window_{screen_.root_window};
};

}  // namespace glasswyrm::server
