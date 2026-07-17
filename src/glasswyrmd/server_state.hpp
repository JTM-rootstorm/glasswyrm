#pragma once

#include "glasswyrmd/atom_table.hpp"
#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/grab_state.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/resource_table.hpp"
#include "glasswyrmd/lifecycle_snapshot.hpp"
#include "glasswyrmd/selection_store.hpp"

#include <limits>
#include <array>
#include <optional>

namespace glasswyrm::server {

struct KeyboardControlState {
  std::uint8_t global_auto_repeat{1};
  std::uint32_t led_mask{};
  std::uint8_t key_click_percent{};
  std::uint8_t bell_percent{50};
  std::uint16_t bell_pitch{400};
  std::uint16_t bell_duration{100};
  std::array<std::uint8_t, 32> auto_repeats = [] {
    std::array<std::uint8_t, 32> value{};
    value.fill(0xff);
    return value;
  }();
};

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
  explicit ServerState(ScreenModel screen = kScreenModel,
                       bool game_compat = false);

  [[nodiscard]] const ScreenModel& screen() const noexcept { return screen_; }
  [[nodiscard]] bool game_compat() const noexcept { return game_compat_; }
  [[nodiscard]] ResourceTable& resources() noexcept { return resources_; }
  [[nodiscard]] const ResourceTable& resources() const noexcept {
    return resources_;
  }
  [[nodiscard]] AtomTable& atoms() noexcept { return atoms_; }
  [[nodiscard]] const AtomTable& atoms() const noexcept { return atoms_; }
  [[nodiscard]] SelectionStore& selections() noexcept { return selections_; }
  [[nodiscard]] const SelectionStore& selections() const noexcept {
    return selections_;
  }
  [[nodiscard]] GrabState& grabs() noexcept { return grabs_; }
  [[nodiscard]] const GrabState& grabs() const noexcept { return grabs_; }
  [[nodiscard]] RandRState& randr() noexcept { return randr_; }
  [[nodiscard]] const RandRState& randr() const noexcept { return randr_; }
  [[nodiscard]] KeyboardControlState& keyboard_control() noexcept {
    return keyboard_control_;
  }
  [[nodiscard]] const KeyboardControlState& keyboard_control() const noexcept {
    return keyboard_control_;
  }
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
      window->focus_serial = intent.focus_serial;
      window->requested_x = intent.requested_x;
      window->requested_y = intent.requested_y;
      window->requested_width = intent.requested_width;
      window->requested_height = intent.requested_height;
      window->requested_border_width = intent.requested_border_width;
      window->geometry_serial = intent.geometry_serial;
      window->stack_serial = intent.stack_serial;
      window->stack_sibling = intent.stack_sibling;
      window->stack_mode = intent.stack_mode;
      window->transient_for = intent.transient_for;
      window->policy_window_type = intent.policy_window_type;
      window->decoration_preference = intent.decoration_preference;
      window->fullscreen_requested = intent.fullscreen_requested;
      window->maximized_requested = intent.maximized_requested;
      window->above_requested = intent.above_requested;
      window->bypass_compositor = intent.bypass_compositor;
      window->attention_requested = intent.attention_requested;
      window->input_requested = intent.input_requested;
      window->minimum_width = intent.minimum_width;
      window->minimum_height = intent.minimum_height;
      window->maximum_width = intent.maximum_width;
      window->maximum_height = intent.maximum_height;
      window->saved_normal_geometry = intent.saved_normal_geometry;
      window->attributes.override_redirect = intent.override_redirect;
      policy.push_back({xid, intent.applied_x, intent.applied_y,
                        intent.applied_width, intent.applied_height,
                        intent.stacking, intent.policy_visible,
                        intent.focused});
    }
    if (!staged.apply_policy(policy)) return false;
    *this = std::move(staged);
    synchronize_ewmh_root_properties(*this);
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
    const auto plan = staged.resources_.capture_destroy_plan(xid);
    if (!plan) return false;
    for (const auto& entry : plan->postorder)
      (void)staged.selections_.clear_window(entry.xid);
    for (const auto& entry : plan->postorder)
      (void)staged.randr_.clear_window(entry.xid);
    if (staged.resources_.commit_destroy_plan(*plan) != DestroyWindowStatus::Success)
      return false;
    if (!staged.commit_lifecycle(evaluated)) return false;
    *this = std::move(staged);
    return true;
  }

  [[nodiscard]] CleanupResult cleanup_client(ClientId owner) {
    (void)selections_.clear_client(owner);
    (void)grabs_.cleanup_client(owner);
    (void)randr_.clear_client(owner);
    auto result = resources_.cleanup_client(owner);
    (void)randr_.prune_windows(resources_);
    synchronize_ewmh_root_properties(*this);
    return result;
  }
  [[nodiscard]] bool invariants_hold() const noexcept {
    return resources_.invariants_hold();
  }

 private:
  ScreenModel screen_;
  bool game_compat_{};
  ResourceTable resources_;
  AtomTable atoms_;
  SelectionStore selections_;
  GrabState grabs_;
  RandRState randr_;
  KeyboardControlState keyboard_control_;
  LifecycleSerialSource lifecycle_serials_;
  std::uint32_t focused_window_{screen_.root_window};
};

}  // namespace glasswyrm::server
