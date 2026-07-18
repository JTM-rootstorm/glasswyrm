#include "wm/policy_engine_internal.hpp"

#include <limits>
#include <set>

namespace glasswyrm::wm::detail {
namespace {

bool extent_fits(const std::int32_t origin, const std::uint32_t extent) {
  return extent != 0 &&
         static_cast<std::int64_t>(origin) + extent - 1 <=
             std::numeric_limits<std::int32_t>::max();
}

bool known_metadata(const RawWindow& window) {
  const auto known_window_type = window.window_type == WindowType::Unknown ||
                                 window.window_type == WindowType::Normal ||
                                 window.window_type == WindowType::Dialog ||
                                 window.window_type == WindowType::Utility;
  const auto known_decoration =
      window.decoration_preference == DecorationPreference::Unknown ||
      window.decoration_preference == DecorationPreference::False ||
      window.decoration_preference == DecorationPreference::True;
  const auto known_stack_mode = window.stack_mode == StackMode::None ||
                                window.stack_mode == StackMode::Above ||
                                window.stack_mode == StackMode::Below;
  return known_window_type && known_decoration && known_stack_mode;
}

EvaluationError validate_window(const RawState& raw,
                                const RawWindow& window,
                                std::set<std::uint64_t>& creation_serials) {
  const auto id = window.window_id;
  if (id == 0 || id == raw.context.root_window_id ||
      window.parent_window_id != raw.context.root_window_id ||
      window.creation_serial == 0 || window.requested_width == 0 ||
      window.requested_height == 0 ||
      window.requested_width > maximum_window_extent ||
      window.requested_height > maximum_window_extent ||
      window.border_width > maximum_border_width ||
      (window.flags & ~kKnownWindowFlags) != 0 || !known_metadata(window) ||
      (window.stack_serial == 0 &&
       (window.stack_sibling != 0 || window.stack_mode != StackMode::None)) ||
      (window.stack_serial != 0 && window.stack_mode == StackMode::None) ||
      window.stack_sibling == id ||
      (window.wants_map && window.map_serial == 0) ||
      !extent_fits(window.requested_x, window.requested_width) ||
      !extent_fits(window.requested_y, window.requested_height) ||
      !creation_serials.insert(window.creation_serial).second)
    return EvaluationError::InvalidWindow;
  if (window.workspace_id != 0 &&
      window.workspace_id != raw.context.workspace_id)
    return EvaluationError::UnsupportedMetadata;
  if (window.transient_for == id) return EvaluationError::InvalidWindow;
  if (window.transient_for != 0) {
    const auto target = raw.windows.find(window.transient_for);
    if (target == raw.windows.end() || target->second.override_redirect)
      return EvaluationError::UnknownReference;
  }
  if (window.stack_serial == 0) return EvaluationError::None;
  if (window.transient_for != 0) return EvaluationError::UnsupportedMetadata;
  if (window.stack_sibling == 0) return EvaluationError::None;
  const auto sibling = raw.windows.find(window.stack_sibling);
  if (sibling == raw.windows.end()) return EvaluationError::UnknownReference;
  if (sibling->second.transient_for != 0 ||
      sibling->second.override_redirect != window.override_redirect)
    return EvaluationError::UnsupportedMetadata;
  return EvaluationError::None;
}

bool transient_cycle(const RawState& raw, const std::uint32_t id) {
  std::set<std::uint32_t> visited;
  auto current = id;
  while (current != 0) {
    if (!visited.insert(current).second) return true;
    const auto found = raw.windows.find(current);
    if (found == raw.windows.end()) break;
    current = found->second.transient_for;
  }
  return false;
}

}  // namespace

EvaluationError validate(const RawState& raw) {
  if (!raw.complete || !raw.has_context)
    return EvaluationError::IncompleteSnapshot;
  const auto& context = raw.context;
  if (context.root_window_id == 0 || context.workspace_id == 0 ||
      context.output_id == 0 || context.flags != 0 || context.work_width == 0 ||
      context.work_height == 0 || context.work_width > maximum_work_extent ||
      context.work_height > maximum_work_extent ||
      !extent_fits(context.work_x, context.work_width) ||
      !extent_fits(context.work_y, context.work_height))
    return EvaluationError::InvalidContext;
  if (raw.windows.size() > maximum_windows) return EvaluationError::Limit;

  std::set<std::uint64_t> creation_serials;
  for (const auto& [id, window] : raw.windows) {
    if (id != window.window_id) return EvaluationError::InvalidWindow;
    const auto error = validate_window(raw, window, creation_serials);
    if (error != EvaluationError::None) return error;
  }
  for (const auto& [id, window] : raw.windows) {
    (void)window;
    if (transient_cycle(raw, id)) return EvaluationError::InvalidWindow;
  }
  return EvaluationError::None;
}

}  // namespace glasswyrm::wm::detail
