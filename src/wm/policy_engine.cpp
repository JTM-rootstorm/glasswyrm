#include "wm/policy_engine.hpp"

#include "wm/policy_engine_internal.hpp"

namespace glasswyrm::wm {

Evaluation evaluate(const RawState& raw, const std::uint64_t generation) {
  Evaluation result;
  result.error = detail::validate(raw);
  if (result.error != EvaluationError::None || generation == 0) {
    if (generation == 0 && result.error == EvaluationError::None)
      result.error = EvaluationError::InvalidWindow;
    return result;
  }

  auto& policy = result.policy;
  policy.generation = generation;
  policy.context = raw.context;
  if (raw.outputs.empty()) {
    detail::apply_placement_and_fullscreen_geometry(raw, policy);
    detail::assign_outputs(raw, policy);
  } else {
    detail::apply_multi_output_geometry(raw, policy);
  }
  detail::apply_focus_and_visibility(raw, policy);
  detail::apply_stacking_and_transient_ordering(raw, policy);
  policy.hash = detail::policy_hash(policy);
  return result;
}

}  // namespace glasswyrm::wm
