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
  [[nodiscard]] bool commit_lifecycle(const LifecycleSnapshot& snapshot) {
    ServerState staged = *this;
    std::vector<AppliedPolicyWindow> policy;
    policy.reserve(snapshot.windows.size());
    for (const auto& [xid, intent] : snapshot.windows) {
      auto* window = staged.resources_.find_window(xid);
      if (!window || !staged.resources_.is_policy_candidate(xid)) return false;
      window->map_requested = intent.map_requested;
      window->map_serial = intent.map_serial;
      window->requested_x = intent.requested_x;
      window->requested_y = intent.requested_y;
      window->requested_width = intent.requested_width;
      window->requested_height = intent.requested_height;
      window->requested_border_width = intent.requested_border_width;
      window->geometry_serial = intent.geometry_serial;
      window->stack_serial = intent.stack_serial;
      window->stack_sibling = intent.stack_sibling;
      window->stack_mode = intent.stack_mode;
      window->attributes.override_redirect = intent.override_redirect;
      policy.push_back({xid, intent.applied_x, intent.applied_y,
                        intent.applied_width, intent.applied_height,
                        intent.stacking, intent.policy_visible,
                        intent.focused});
    }
    if (!staged.apply_policy(policy)) return false;
    *this = std::move(staged);
    return true;
  }
  [[nodiscard]] std::optional<LifecycleSnapshot> propose_create_lifecycle(
      const ClientId owner, const std::uint32_t resource_base,
      const std::uint32_t resource_mask, const WindowCreateSpec& spec,
      const std::uint64_t creation_serial) const {
    auto staged = *this;
    if (staged.resources_.create_window(owner, resource_base, resource_mask,
                                        spec) != CreateWindowStatus::Success)
      return std::nullopt;
    auto* window = staged.resources_.find_window(spec.xid);
    if (!window || !staged.resources_.is_policy_candidate(spec.xid))
      return std::nullopt;
    window->creation_serial = creation_serial;
    return staged.lifecycle_snapshot();
  }
  [[nodiscard]] std::optional<LifecycleSnapshot> propose_destroy_lifecycle(
      const std::uint32_t xid) const {
    if (!resources_.is_policy_candidate(xid)) return std::nullopt;
    auto proposed = lifecycle_snapshot();
    proposed.windows.erase(xid);
    std::erase(proposed.root_order, xid);
    if (proposed.focused_window == xid)
      proposed.focused_window = proposed.root_window;
    return proposed;
  }
  [[nodiscard]] bool commit_create_lifecycle(
      const ClientId owner, const std::uint32_t resource_base,
      const std::uint32_t resource_mask, const WindowCreateSpec& spec,
      const std::uint64_t creation_serial,
      const LifecycleSnapshot& evaluated) {
    ServerState staged = *this;
    if (staged.resources_.create_window(owner, resource_base, resource_mask,
                                        spec) != CreateWindowStatus::Success)
      return false;
    auto* window = staged.resources_.find_window(spec.xid);
    if (!window) return false;
    window->creation_serial = creation_serial;
    if (!staged.commit_lifecycle(evaluated)) return false;
    *this = std::move(staged);
    return true;
  }
  [[nodiscard]] bool commit_destroy_lifecycle(
      const std::uint32_t xid, const LifecycleSnapshot& evaluated) {
    ServerState staged = *this;
    if (staged.resources_.destroy_window(xid) != DestroyWindowStatus::Success)
      return false;
    if (!staged.commit_lifecycle(evaluated)) return false;
    *this = std::move(staged);
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
