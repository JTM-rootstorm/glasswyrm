#ifndef GLASSWYRM_WM_TRANSACTION_HPP
#define GLASSWYRM_WM_TRANSACTION_HPP

#include "wm/policy_engine.hpp"

#include <functional>
#include <optional>

namespace glasswyrm::wm {

class Transaction {
 public:
  using OutputPreflight = std::function<bool(const PolicyState&)>;

  [[nodiscard]] bool begin_snapshot();
  [[nodiscard]] bool end_snapshot();
  [[nodiscard]] bool abort_snapshot();
  [[nodiscard]] bool upsert(const Context& context);
  [[nodiscard]] bool upsert(const RawWindow& window);
  [[nodiscard]] bool remove(std::uint32_t window_id);
  [[nodiscard]] Evaluation commit(std::uint64_t generation,
                                  const OutputPreflight& preflight = {});
  void disconnect() noexcept;

  [[nodiscard]] bool snapshot_active() const noexcept { return snapshot_active_; }
  [[nodiscard]] const RawState& pending() const noexcept { return pending_; }
  [[nodiscard]] const RawState& committed_raw() const noexcept { return committed_raw_; }
  [[nodiscard]] const PolicyState& committed_policy() const noexcept {
    return committed_policy_;
  }

 private:
  RawState pending_;
  RawState committed_raw_;
  PolicyState committed_policy_;
  std::optional<RawState> pre_snapshot_;
  bool snapshot_active_{};
};

}  // namespace glasswyrm::wm

#endif
