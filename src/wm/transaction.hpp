#ifndef GLASSWYRM_WM_TRANSACTION_HPP
#define GLASSWYRM_WM_TRANSACTION_HPP

#include "wm/policy_engine.hpp"
#include "wm/vrr_policy.hpp"

#include <functional>
#include <utility>

namespace glasswyrm::wm {

class Transaction {
 public:
  using OutputPreflight = std::function<bool(const PolicyState&)>;

  [[nodiscard]] bool begin_snapshot();
  [[nodiscard]] bool end_snapshot();
  [[nodiscard]] bool abort_snapshot();
  [[nodiscard]] bool upsert(const Context& context);
  [[nodiscard]] bool upsert(const OutputContext& output);
  [[nodiscard]] bool upsert(const WindowOutputHint& hint);
  [[nodiscard]] bool upsert(const RawWindow& window);
  [[nodiscard]] bool upsert(const VrrOutputInput& output);
  [[nodiscard]] bool upsert(const VrrWindowInput& window);
  [[nodiscard]] bool remove(std::uint32_t window_id);
  [[nodiscard]] Evaluation commit(std::uint64_t generation,
                                  const OutputPreflight& preflight = {});
  void disconnect() noexcept;
  void set_committed_policy_hash(std::uint64_t hash) noexcept {
    committed_policy_.hash = hash;
  }

  [[nodiscard]] bool snapshot_active() const noexcept { return snapshot_active_; }
  [[nodiscard]] const RawState& pending() const noexcept { return pending_; }
  [[nodiscard]] const RawState& committed_raw() const noexcept { return committed_raw_; }
  [[nodiscard]] const PolicyState& committed_policy() const noexcept {
    return committed_policy_;
  }
  [[nodiscard]] const VrrInputs& pending_vrr() const noexcept {
    return pending_vrr_;
  }
  [[nodiscard]] const VrrPolicyState& committed_vrr() const noexcept {
    return committed_vrr_;
  }
  void set_committed_vrr(VrrPolicyState policy) {
    committed_vrr_ = std::move(policy);
  }

 private:
  RawState pending_;
  RawState committed_raw_;
  PolicyState committed_policy_;
  RawState pre_snapshot_;
  VrrInputs pending_vrr_;
  VrrPolicyState committed_vrr_;
  VrrInputs pre_snapshot_vrr_;
  bool snapshot_active_{};
};

}  // namespace glasswyrm::wm

#endif
