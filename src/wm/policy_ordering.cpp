#include "wm/policy_engine_internal.hpp"

#include <algorithm>
#include <tuple>

namespace glasswyrm::wm::detail {
namespace {

auto stack_key(const RawWindow& window) {
  return std::tuple((window.flags & kAboveFlag) != 0, window.map_serial,
                    window.creation_serial, window.window_id);
}

void sort_ids(const RawState& raw, std::vector<std::uint32_t>& ids) {
  std::sort(ids.begin(), ids.end(), [&](const auto left, const auto right) {
    return stack_key(raw.windows.at(left)) < stack_key(raw.windows.at(right));
  });
}

void apply_restack(const RawState& raw, std::vector<std::uint32_t>& band) {
  auto operations = band;
  std::erase_if(operations, [&](const auto id) {
    return raw.windows.at(id).stack_serial == 0;
  });
  std::sort(operations.begin(), operations.end(), [&](const auto left,
                                                      const auto right) {
    const auto& l = raw.windows.at(left);
    const auto& r = raw.windows.at(right);
    return std::tie(l.stack_serial, l.creation_serial, l.window_id) <
           std::tie(r.stack_serial, r.creation_serial, r.window_id);
  });
  for (const auto id : operations) {
    const auto& operation = raw.windows.at(id);
    const auto current = std::find(band.begin(), band.end(), id);
    if (current == band.end()) continue;
    band.erase(current);
    if (operation.stack_sibling == 0) {
      if (operation.stack_mode == StackMode::Above)
        band.push_back(id);
      else
        band.insert(band.begin(), id);
      continue;
    }
    auto sibling = std::find(band.begin(), band.end(), operation.stack_sibling);
    if (operation.stack_mode == StackMode::Above) ++sibling;
    band.insert(sibling, id);
  }
}

void emit_transient_tree(const RawState& raw, const PolicyState& policy,
                         const std::uint32_t id,
                         std::vector<std::uint32_t>& stacking) {
  stacking.push_back(id);
  std::vector<std::uint32_t> children;
  for (const auto& [child_id, child] : raw.windows) {
    if (!child.override_redirect && child.transient_for == id &&
        policy.windows.at(child_id).visible)
      children.push_back(child_id);
  }
  sort_ids(raw, children);
  for (const auto child : children)
    emit_transient_tree(raw, policy, child, stacking);
}

}  // namespace

std::vector<std::uint32_t> transient_parent_first(const RawState& raw) {
  std::vector<std::uint32_t> ordered;
  for (const auto& [id, window] : raw.windows) {
    if (!window.override_redirect && window.transient_for != 0)
      ordered.push_back(id);
  }
  const auto depth = [&](const std::uint32_t id) {
    std::size_t value = 0;
    auto current = raw.windows.at(id).transient_for;
    while (current != 0) {
      ++value;
      current = raw.windows.at(current).transient_for;
    }
    return value;
  };
  std::sort(ordered.begin(), ordered.end(), [&](const auto left,
                                                const auto right) {
    return std::tuple(depth(left), left) < std::tuple(depth(right), right);
  });
  return ordered;
}

void apply_stacking_and_transient_ordering(const RawState& raw,
                                           PolicyState& policy) {
  std::vector<std::uint32_t> managed_roots;
  std::vector<std::uint32_t> overrides;
  for (const auto& [id, window] : raw.windows) {
    if (window.override_redirect)
      overrides.push_back(id);
    else if (window.transient_for == 0)
      managed_roots.push_back(id);
  }
  sort_ids(raw, managed_roots);
  sort_ids(raw, overrides);
  apply_restack(raw, managed_roots);
  apply_restack(raw, overrides);
  std::stable_partition(managed_roots.begin(), managed_roots.end(),
                        [&](const auto id) {
    return policy.windows.at(id).applied_state != AppliedState::Fullscreen;
  });
  std::erase_if(managed_roots, [&](const auto id) {
    return !policy.windows.at(id).visible;
  });
  std::erase_if(overrides, [&](const auto id) {
    return !policy.windows.at(id).visible;
  });

  std::vector<std::uint32_t> stacking;
  for (const auto id : managed_roots)
    emit_transient_tree(raw, policy, id, stacking);
  stacking.insert(stacking.end(), overrides.begin(), overrides.end());
  for (std::size_t index = 0; index < stacking.size(); ++index)
    policy.windows.at(stacking[index]).stacking =
        static_cast<std::int32_t>(index);

  policy.output_order = std::move(stacking);
  for (const auto& [id, state] : policy.windows) {
    if (!state.visible) policy.output_order.push_back(id);
  }
}

}  // namespace glasswyrm::wm::detail
