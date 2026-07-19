#include "wm/vrr_validation.hpp"

#include <set>

namespace glasswyrm::wm {
namespace {

bool valid_mode(const VrrPolicyMode mode) noexcept {
  return mode >= VrrPolicyMode::Off &&
         mode <= VrrPolicyMode::AlwaysEligible;
}

bool valid_preference(const VrrWindowPreference preference) noexcept {
  return preference >= VrrWindowPreference::Default &&
         preference <= VrrWindowPreference::Prefer;
}

}  // namespace

VrrEvaluationError validate_vrr_inputs(const RawState& raw,
                                       const PolicyState& base,
                                       const VrrInputs& inputs) noexcept {
  if (!inputs.complete)
    return VrrEvaluationError::IncompleteSnapshot;
  if (!raw.complete || !raw.has_context || base.generation == 0 ||
      base.hash == 0 || base.outputs.size() != raw.outputs.size() ||
      base.windows.size() != raw.windows.size())
    return VrrEvaluationError::BasePolicyMismatch;
  if (inputs.outputs.size() != base.outputs.size() ||
      inputs.outputs.size() > maximum_outputs)
    return VrrEvaluationError::InvalidOutput;

  for (const auto& [id, output] : inputs.outputs) {
    if (id == 0 || id != output.output_id || output.flags != 0 ||
        !valid_mode(output.mode))
      return VrrEvaluationError::InvalidOutput;
    const auto base_output = base.outputs.find(id);
    const auto raw_output = raw.outputs.find(id);
    if (base_output == base.outputs.end() || raw_output == raw.outputs.end())
      return VrrEvaluationError::UnknownReference;
  }

  std::size_t managed_count = 0;
  std::size_t focused_count = 0;
  for (const auto& [id, window] : base.windows) {
    const auto raw_window = raw.windows.find(id);
    if (raw_window == raw.windows.end() || window.window_id != id ||
        raw_window->second.window_id != id ||
        !base.outputs.contains(window.output_id) ||
        !inputs.outputs.contains(window.output_id))
      return VrrEvaluationError::BasePolicyMismatch;
    focused_count += window.focused;
    if (window.managed && !window.override_redirect)
      ++managed_count;
  }
  if (focused_count > 1)
    return VrrEvaluationError::BasePolicyMismatch;
  if (inputs.windows.size() != managed_count ||
      inputs.windows.size() > maximum_windows)
    return VrrEvaluationError::InvalidWindow;

  for (const auto& [id, input] : inputs.windows) {
    if (id == 0 || id != input.window_id || input.flags != 0 ||
        !valid_preference(input.preference))
      return VrrEvaluationError::InvalidWindow;
    const auto window = base.windows.find(id);
    if (window == base.windows.end() || !window->second.managed ||
        window->second.override_redirect)
      return VrrEvaluationError::UnknownReference;
    if (input.output_membership.size() > maximum_outputs)
      return VrrEvaluationError::Limit;
    std::set<std::uint64_t> unique;
    for (const auto output_id : input.output_membership) {
      const auto output = base.outputs.find(output_id);
      if (output_id == 0 || output == base.outputs.end() ||
          !output->second.enabled)
        return VrrEvaluationError::UnknownReference;
      if (!unique.insert(output_id).second)
        return VrrEvaluationError::InvalidWindow;
    }
  }
  return VrrEvaluationError::None;
}

}  // namespace glasswyrm::wm
