#include "wm/transaction.hpp"

namespace glasswyrm::wm {

bool Transaction::begin_snapshot() {
  if (snapshot_active_) return false;
  pre_snapshot_ = pending_;
  pending_ = {};
  snapshot_active_ = true;
  return true;
}

bool Transaction::end_snapshot() {
  if (!snapshot_active_ || !pending_.has_context) return false;
  pending_.complete = true;
  snapshot_active_ = false;
  pre_snapshot_.reset();
  return true;
}

bool Transaction::abort_snapshot() {
  if (!snapshot_active_ || !pre_snapshot_) return false;
  pending_ = std::move(*pre_snapshot_);
  pre_snapshot_.reset();
  snapshot_active_ = false;
  return true;
}

bool Transaction::upsert(const Context& context) {
  if (!snapshot_active_ && !committed_raw_.complete) return false;
  if (snapshot_active_ && pending_.has_context) return false;
  pending_.context = context;
  pending_.has_context = true;
  return true;
}

bool Transaction::upsert(const OutputContext& output) {
  if (!snapshot_active_ && !committed_raw_.complete) return false;
  if (!pending_.outputs.contains(output.output_id) &&
      pending_.outputs.size() >= maximum_outputs)
    return false;
  pending_.outputs[output.output_id] = output;
  return true;
}

bool Transaction::upsert(const WindowOutputHint& hint) {
  if (!snapshot_active_ && !committed_raw_.complete) return false;
  if (!pending_.output_hints.contains(hint.window_id) &&
      pending_.output_hints.size() >= maximum_windows)
    return false;
  pending_.output_hints[hint.window_id] = hint;
  return true;
}

bool Transaction::upsert(const RawWindow& window) {
  if (!snapshot_active_ && !committed_raw_.complete) return false;
  if (!pending_.windows.contains(window.window_id) &&
      pending_.windows.size() >= maximum_windows) return false;
  pending_.windows[window.window_id] = window;
  return true;
}

bool Transaction::remove(const std::uint32_t window_id) {
  if (snapshot_active_ || !committed_raw_.complete || window_id == 0 ||
      pending_.windows.erase(window_id) != 1)
    return false;
  pending_.output_hints.erase(window_id);
  return true;
}

Evaluation Transaction::commit(const std::uint64_t generation,
                               const OutputPreflight& preflight) {
  if (snapshot_active_) return {EvaluationError::IncompleteSnapshot, {}};
  if (generation == 0 ||
      (committed_raw_.producer_generation != 0 &&
       generation < committed_raw_.producer_generation))
    return {EvaluationError::InvalidWindow, {}};
  auto candidate = pending_;
  candidate.producer_generation = generation;
  auto evaluated = evaluate(candidate, generation);
  if (!evaluated) return evaluated;
  if (preflight && !preflight(evaluated.policy)) {
    evaluated.error = EvaluationError::OutputFailure;
    evaluated.policy = {};
    return evaluated;
  }
  committed_raw_ = std::move(candidate);
  committed_policy_ = evaluated.policy;
  pending_ = committed_raw_;
  return evaluated;
}

void Transaction::disconnect() noexcept {
  pending_ = {};
  committed_raw_ = {};
  committed_policy_ = {};
  pre_snapshot_.reset();
  snapshot_active_ = false;
}

}  // namespace glasswyrm::wm
