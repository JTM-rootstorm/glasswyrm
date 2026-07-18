#include "wm/policy_engine_internal.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <vector>

namespace glasswyrm::wm::detail {
namespace {

bool extent_fits(const std::int32_t origin, const std::uint32_t extent) {
  return extent != 0 &&
         static_cast<std::int64_t>(origin) + extent - 1 <=
             std::numeric_limits<std::int32_t>::max();
}

bool valid_rectangle(const Rectangle& rectangle) {
  return rectangle.x >= 0 && rectangle.y >= 0 && rectangle.width != 0 &&
         rectangle.height != 0 && rectangle.width <= maximum_work_extent &&
         rectangle.height <= maximum_work_extent &&
         extent_fits(rectangle.x, rectangle.width) &&
         extent_fits(rectangle.y, rectangle.height);
}

bool contains(const Rectangle& outer, const Rectangle& inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         static_cast<std::int64_t>(inner.x) + inner.width <=
             static_cast<std::int64_t>(outer.x) + outer.width &&
         static_cast<std::int64_t>(inner.y) + inner.height <=
             static_cast<std::int64_t>(outer.y) + outer.height;
}

bool overlaps(const Rectangle& left, const Rectangle& right) {
  return left.x < static_cast<std::int64_t>(right.x) + right.width &&
         right.x < static_cast<std::int64_t>(left.x) + left.width &&
         left.y < static_cast<std::int64_t>(right.y) + right.height &&
         right.y < static_cast<std::int64_t>(left.y) + left.height;
}

bool valid_scale(const OutputContext& output) {
  return output.scale_numerator != 0 && output.scale_denominator != 0 &&
         output.scale_denominator <= 120 &&
         std::gcd(output.scale_numerator, output.scale_denominator) == 1 &&
         output.scale_numerator >= output.scale_denominator &&
         static_cast<std::uint64_t>(output.scale_numerator) <=
             UINT64_C(4) * output.scale_denominator;
}

EvaluationError validate_outputs(const RawState& raw) {
  if (raw.outputs.size() > maximum_outputs)
    return EvaluationError::Limit;
  std::size_t enabled_count = 0;
  std::size_t primary_count = 0;
  std::int64_t maximum_x = 0;
  std::int64_t maximum_y = 0;
  std::vector<const OutputContext*> enabled_outputs;
  for (const auto& [id, output] : raw.outputs) {
    if (id == 0 || id != output.output_id || output.flags != 0 ||
        static_cast<std::uint8_t>(output.transform) >
            static_cast<std::uint8_t>(OutputTransform::Flipped270) ||
        !valid_scale(output) || (output.primary && !output.enabled))
      return EvaluationError::InvalidContext;
    if (!output.enabled) {
      if (output.primary || output.logical.x != 0 || output.logical.y != 0 ||
          output.logical.width != 0 || output.logical.height != 0 ||
          output.work.x != 0 || output.work.y != 0 ||
          output.work.width != 0 || output.work.height != 0)
        return EvaluationError::InvalidContext;
      continue;
    }
    if (!valid_rectangle(output.logical) || !valid_rectangle(output.work) ||
        !contains(output.logical, output.work))
      return EvaluationError::InvalidContext;
    ++enabled_count;
    primary_count += output.primary;
    maximum_x = std::max(maximum_x, static_cast<std::int64_t>(output.logical.x) +
                                        output.logical.width);
    maximum_y = std::max(maximum_y, static_cast<std::int64_t>(output.logical.y) +
                                        output.logical.height);
    enabled_outputs.push_back(&output);
  }
  for (std::size_t left = 0; left < enabled_outputs.size(); ++left)
    for (std::size_t right = left + 1; right < enabled_outputs.size(); ++right)
      if (overlaps(enabled_outputs[left]->logical,
                   enabled_outputs[right]->logical))
        return EvaluationError::InvalidContext;
  if (enabled_count == 0 || primary_count != 1 ||
      !raw.outputs.contains(raw.context.output_id) ||
      !raw.outputs.at(raw.context.output_id).primary ||
      raw.context.work_x != 0 || raw.context.work_y != 0 ||
      static_cast<std::int64_t>(raw.context.work_width) != maximum_x ||
      static_cast<std::int64_t>(raw.context.work_height) != maximum_y)
    return EvaluationError::InvalidContext;
  return EvaluationError::None;
}

EvaluationError validate_hints(const RawState& raw) {
  if (raw.output_hints.size() > raw.windows.size())
    return EvaluationError::Limit;
  for (const auto& [id, hint] : raw.output_hints)
    if (id == 0 || id != hint.window_id || hint.flags != 0 ||
        !raw.windows.contains(id))
      return EvaluationError::InvalidWindow;
  return EvaluationError::None;
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
      context.work_height == 0 ||
      context.work_width >
          (raw.outputs.empty() ? maximum_work_extent : maximum_root_extent) ||
      context.work_height >
          (raw.outputs.empty() ? maximum_work_extent : maximum_root_extent) ||
      !extent_fits(context.work_x, context.work_width) ||
      !extent_fits(context.work_y, context.work_height))
    return EvaluationError::InvalidContext;
  if (!raw.outputs.empty()) {
    const auto output_error = validate_outputs(raw);
    if (output_error != EvaluationError::None) return output_error;
    const auto hint_error = validate_hints(raw);
    if (hint_error != EvaluationError::None) return hint_error;
  } else if (!raw.output_hints.empty()) {
    return EvaluationError::UnsupportedMetadata;
  }
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
