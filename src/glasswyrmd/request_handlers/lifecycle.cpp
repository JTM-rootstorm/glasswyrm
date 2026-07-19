#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/gw_scale_state.hpp"

#include "glasswyrmd/request_handlers/drawable_access.hpp"
#include "glasswyrmd/request_handlers/window_attributes.hpp"
#include "core/geometry/region.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/event_mask.hpp"
#include "protocol/x11/lifecycle_request.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

std::optional<StructuralEventState> capture_structural_state(
    const ResourceTable& resources, const std::uint32_t target) {
  const auto* window = resources.find_window(target);
  if (!window) return std::nullopt;
  const auto* parent = resources.find_window(window->parent);
  StructuralEventState state{};
  state.target = target;
  state.parent = window->parent;
  state.x = window->x;
  state.y = window->y;
  state.width = window->width;
  state.height = window->height;
  state.border_width = window->border_width;
  state.override_redirect = window->attributes.override_redirect;
  state.mapped = window->map_requested;
  state.viewable = window->map_state == MapState::Viewable;
  if (parent) {
    const auto position = std::ranges::find(parent->children, target);
    if (position != parent->children.end() &&
        std::next(position) != parent->children.end())
      state.above_sibling = *std::next(position);
  }
  for (const auto& [client, mask] : window->event_selections)
    if ((mask & x11::event_mask::StructureNotify) != 0)
      state.structure_recipients.push_back(client);
  if (parent)
    for (const auto& [client, mask] : parent->event_selections)
      if ((mask & x11::event_mask::SubstructureNotify) != 0)
        state.substructure_recipients.push_back(client);
  std::sort(state.structure_recipients.begin(),
            state.structure_recipients.end());
  std::sort(state.substructure_recipients.begin(),
            state.substructure_recipients.end());
  return state;
}

constexpr std::uint32_t kWindowAttributeMask = 0x00007fffU;

std::optional<geometry::Rectangle> child_outer_rectangle(
    const StructuralEventState& state) {
  const auto extent_width = static_cast<std::uint32_t>(state.width) +
                            static_cast<std::uint32_t>(state.border_width) * 2U;
  const auto extent_height = static_cast<std::uint32_t>(state.height) +
                             static_cast<std::uint32_t>(state.border_width) * 2U;
  return geometry::Rectangle{state.x, state.y, extent_width, extent_height};
}

void add_child_outer_damage(DispatchResult& result,
                            ServerState& server_state,
                            const StructuralEventState& state,
                            const std::uint32_t timestamp) {
  const auto rectangle = child_outer_rectangle(state);
  if (rectangle)
    add_drawable_damage(result, server_state, state.parent, *rectangle,
                        timestamp);
}

void add_parent_reveal(DispatchResult& result, const ResourceTable& resources,
                       const StructuralEventState& state) {
  const auto* parent = resources.find_window(state.parent);
  const auto rectangle = child_outer_rectangle(state);
  if (!parent || !rectangle) return;
  const auto clipped = geometry::intersect(
      *rectangle, {0, 0, parent->width, parent->height});
  if (clipped) result.expose_intents.push_back({state.parent, *clipped});
}

void add_local_lifecycle_effects(DispatchResult& result,
                                 ServerState& state,
                                 const StructuralTransition& transition,
                                 const std::uint32_t timestamp) {
  if (!transition.before) return;
  const auto* target = state.resources().find_window(transition.before->target);
  if (!target || target->window_class != WindowClass::InputOutput) return;
  if (transition.kind == StructuralTransitionKind::Map && transition.committed &&
      !transition.before->viewable && transition.committed->viewable) {
    add_drawable_damage(result, state, transition.committed->target,
                        {0, 0, transition.committed->width,
                         transition.committed->height},
                        timestamp);
  } else if (transition.kind == StructuralTransitionKind::Unmap &&
             transition.before->viewable && transition.committed &&
             !transition.committed->viewable) {
    add_child_outer_damage(result, state, *transition.before, timestamp);
    add_parent_reveal(result, state.resources(), *transition.before);
  } else if (transition.kind == StructuralTransitionKind::Configure &&
             transition.committed) {
    add_child_outer_damage(result, state, *transition.before, timestamp);
    add_child_outer_damage(result, state, *transition.committed, timestamp);
  } else if (transition.kind == StructuralTransitionKind::Destroy &&
             transition.before->viewable) {
    add_child_outer_damage(result, state, *transition.before, timestamp);
    add_parent_reveal(result, state.resources(), *transition.before);
  }
}

void invalidate_scaled_parent_for_mapped_child(
    DispatchResult& result, ServerState& state, const std::uint32_t child_xid) {
  const auto* child = state.resources().find_window(child_xid);
  if (!child || child->window_class != WindowClass::InputOutput ||
      !child->map_requested)
    return;
  auto* parent = state.resources().find_window(child->parent);
  if (!parent || parent->parent != state.screen().root_window ||
      !invalidate_scaled_pixmap(parent->scale))
    return;
  append_gw_scale_notifications(result, *parent, child->parent,
                                kGwScaleInvalidatedReason);
}
DispatchResult create_window(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.bytes.size() < 32) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.bytes, context.byte_order);
  WindowCreateSpec spec;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t window_class = 0;
  std::uint32_t value_mask = 0;
  if (!reader.skip(4) || !reader.read_u32(spec.xid) ||
      !reader.read_u32(spec.parent) || !reader.read_u16(x) ||
      !reader.read_u16(y) || !reader.read_u16(spec.width) ||
      !reader.read_u16(spec.height) || !reader.read_u16(spec.border_width) ||
      !reader.read_u16(window_class) || !reader.read_u32(spec.visual) ||
      !reader.read_u32(value_mask)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  spec.x = static_cast<std::int16_t>(x);
  spec.y = static_cast<std::int16_t>(y);
  spec.depth = request.data;
  spec.window_class = static_cast<WindowClass>(window_class);
  spec.attribute_mask = value_mask;
  if (window_class > static_cast<std::uint16_t>(WindowClass::InputOnly)) {
    return error(context, request, x11::CoreErrorCode::BadValue, window_class);
  }
  if ((value_mask & ~kWindowAttributeMask) != 0) {
    return error(context, request, x11::CoreErrorCode::BadValue, value_mask);
  }
  const std::size_t value_count =
      static_cast<std::size_t>(std::popcount(value_mask));
  if (value_count > (std::numeric_limits<std::size_t>::max() - 32) / 4 ||
      !exact_size(request, 32 + value_count * 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }

  auto decoded = decode_window_attributes(reader, value_mask, spec.attributes,
                                          state.resources());
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad_value);
  spec.attributes = decoded.attributes;
  spec.initial_event_mask = decoded.event_mask.value_or(0);

  const auto* parent = state.resources().find_window(spec.parent);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(spec.parent))
    return error(context, request, x11::CoreErrorCode::BadWindow, spec.parent);
  const bool policy_candidate =
      parent != nullptr && spec.parent == state.screen().root_window &&
      (spec.window_class == WindowClass::InputOutput ||
       (spec.window_class == WindowClass::CopyFromParent &&
        parent->window_class == WindowClass::InputOutput));
  if (context.integrated_lifecycle && policy_candidate) {
    auto staged = state;
    const auto status = staged.resources().create_window(
        context.client_id, context.resource_base, context.resource_mask, spec);
    switch (status) {
      case CreateWindowStatus::Success:
        return DispatchResult::deferred_create_window(std::move(spec));
      case CreateWindowStatus::BadIdChoice:
        return error(context, request, x11::CoreErrorCode::BadIDChoice,
                     spec.xid);
      case CreateWindowStatus::BadWindow:
        return error(context, request, x11::CoreErrorCode::BadWindow,
                     spec.parent);
      case CreateWindowStatus::BadValue:
        return error(context, request, x11::CoreErrorCode::BadValue);
      case CreateWindowStatus::BadMatch:
        return error(context, request, x11::CoreErrorCode::BadMatch);
      case CreateWindowStatus::BadAlloc:
        return error(context, request, x11::CoreErrorCode::BadAlloc);
    }
  }

  switch (state.resources().create_window(
      context.client_id, context.resource_base, context.resource_mask, spec)) {
    case CreateWindowStatus::Success: return {};
    case CreateWindowStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, spec.xid);
    case CreateWindowStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, spec.parent);
    case CreateWindowStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue, 0);
    case CreateWindowStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch);
    case CreateWindowStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult destroy_window(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  if (!reader.read_u32(window)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (context.integrated_lifecycle && state.resources().cleanup_pending(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if (window == state.screen().root_window) return {};
  if (state.game_compat() && window == kEwmhSupportingWindow)
    return error(context, request, x11::CoreErrorCode::BadAccess, window);
  if (context.integrated_lifecycle &&
      state.resources().is_policy_candidate(window)) {
    return DispatchResult::deferred_destroy_window(window);
  }
  const auto destroyed = state.resources().capture_destroy_plan(window);
  if (!destroyed)
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  DispatchResult result;
  result.structural_transitions.reserve(destroyed->postorder.size());
  for (const auto& item : destroyed->postorder) {
    auto before = capture_structural_state(state.resources(), item.xid)
                      .value_or(StructuralEventState{});
    before.target = item.xid; before.parent = item.parent;
    before.structure_recipients = item.structure_recipients;
    before.substructure_recipients = item.substructure_recipients;
    result.structural_transitions.push_back(
        {StructuralTransitionKind::Destroy, std::move(before), std::nullopt});
  }
  if (!result.structural_transitions.empty())
    add_local_lifecycle_effects(result, state,
                                result.structural_transitions.back(),
                                context.input.logical_time);
  const auto status = state.resources().commit_destroy_plan(*destroyed);
  if (status == DestroyWindowStatus::BadWindow) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  for (const auto& item : destroyed->postorder) {
    for (const auto& [selection, owner] :
         state.selections().owned_by_window(item.xid))
      append_xfixes_notifications(
          result, state.selections().xfixes_notifications(
                      selection, 1, 0, context.input.logical_time,
                      owner.last_change_time));
    (void)state.selections().clear_window(item.xid);
    (void)state.randr().clear_window(item.xid);
    (void)state.composite().remove_window(item.xid);
    state.vrr().erase_window(item.xid);
  }
  return result;
}

DispatchResult lifecycle_decode_error(const DispatchContext& context,
                                      const x11::FramedRequest& request,
                                      x11::LifecycleDecodeStatus status) {
  if (status == x11::LifecycleDecodeStatus::BadValue)
    return error(context, request, x11::CoreErrorCode::BadValue);
  if (status == x11::LifecycleDecodeStatus::BadMatch)
    return error(context, request, x11::CoreErrorCode::BadMatch);
  return error(context, request, x11::CoreErrorCode::BadLength);
}

DispatchResult map_window(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request, bool mapped) {
  x11::WindowLifecycleRequest decoded;
  const auto status = mapped
      ? x11::decode_map_window(request.bytes, context.byte_order, decoded)
      : x11::decode_unmap_window(request.bytes, context.byte_order, decoded);
  if (status != x11::LifecycleDecodeStatus::Complete)
    return lifecycle_decode_error(context, request, status);
  if (!state.resources().find_window(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (decoded.window == state.screen().root_window) return {};
  if (state.resources().is_policy_candidate(decoded.window) &&
      context.integrated_lifecycle)
    return DispatchResult::deferred(decoded.window, {}, mapped);
  if (state.resources().is_policy_candidate(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto before = capture_structural_state(state.resources(), decoded.window);
  switch (state.resources().set_local_map_intent(decoded.window, mapped)) {
    case LocalLifecycleStatus::Success: {
      DispatchResult result;
      result.structural_transitions.push_back(
          {mapped ? StructuralTransitionKind::Map
                  : StructuralTransitionKind::Unmap,
           before, capture_structural_state(state.resources(), decoded.window)});
      add_local_lifecycle_effects(result, state,
                                  result.structural_transitions.back(),
                                  context.input.logical_time);
      if (mapped)
        invalidate_scaled_parent_for_mapped_child(result, state,
                                                  decoded.window);
      return result;
    }
    case LocalLifecycleStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
    case LocalLifecycleStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
    case LocalLifecycleStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue);
    case LocalLifecycleStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult configure_window(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  x11::ConfigureWindowRequest decoded;
  const auto status = x11::decode_configure_window(request.bytes,
                                                    context.byte_order, decoded);
  if (status != x11::LifecycleDecodeStatus::Complete)
    return lifecycle_decode_error(context, request, status);
  if (!state.resources().find_window(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (decoded.window == state.screen().root_window)
    return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
  if (decoded.stack_mode && *decoded.stack_mode != x11::CoreStackMode::Above &&
      *decoded.stack_mode != x11::CoreStackMode::Below)
    return error(context, request, x11::CoreErrorCode::BadImplementation,
                 static_cast<std::uint32_t>(*decoded.stack_mode));
  if (state.resources().is_policy_candidate(decoded.window) &&
      context.integrated_lifecycle)
    return DispatchResult::deferred(decoded.window, decoded);
  if (state.resources().is_policy_candidate(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  LocalConfigure local{decoded.x, decoded.y, decoded.width, decoded.height,
                       decoded.border_width, decoded.sibling,
                       decoded.stack_mode == x11::CoreStackMode::Above
                           ? LifecycleStackMode::Above
                           : decoded.stack_mode == x11::CoreStackMode::Below
                                 ? LifecycleStackMode::Below
                                 : LifecycleStackMode::None};
  const auto before = capture_structural_state(state.resources(), decoded.window);
  switch (state.resources().configure_local(decoded.window, local)) {
    case LocalLifecycleStatus::Success: {
      DispatchResult result;
      result.structural_transitions.push_back(
          {StructuralTransitionKind::Configure, before,
           capture_structural_state(state.resources(), decoded.window)});
      add_local_lifecycle_effects(result, state,
                                  result.structural_transitions.back(),
                                  context.input.logical_time);
      return result;
    }
    case LocalLifecycleStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
    case LocalLifecycleStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
    case LocalLifecycleStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue);
    case LocalLifecycleStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult map_subwindows(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request,
                              const bool mapped) {
  x11::WindowLifecycleRequest decoded;
  const auto status = mapped
      ? x11::decode_map_subwindows(request.bytes, context.byte_order, decoded)
      : x11::decode_unmap_subwindows(request.bytes, context.byte_order, decoded);
  if (status != x11::LifecycleDecodeStatus::Complete)
    return lifecycle_decode_error(context, request, status);
  const auto* parent = state.resources().find_window(decoded.window);
  if (!parent)
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  const auto children = parent->children;
  if (std::ranges::any_of(children, [&](const std::uint32_t child) {
        return state.resources().is_policy_candidate(child);
      }))
    return error(context, request, x11::CoreErrorCode::BadImplementation);

  DispatchResult result;
  result.structural_transitions.reserve(children.size());
  for (const auto child : children) {
    const auto before = capture_structural_state(state.resources(), child);
    const auto transition_status =
        state.resources().set_local_map_intent(child, mapped);
    if (transition_status != LocalLifecycleStatus::Success) {
      const auto code = transition_status == LocalLifecycleStatus::BadWindow
                            ? x11::CoreErrorCode::BadWindow
                        : transition_status == LocalLifecycleStatus::BadMatch
                            ? x11::CoreErrorCode::BadMatch
                        : transition_status == LocalLifecycleStatus::BadAlloc
                            ? x11::CoreErrorCode::BadAlloc
                            : x11::CoreErrorCode::BadValue;
      return error(context, request, code, child);
    }
    result.structural_transitions.push_back(
        {mapped ? StructuralTransitionKind::Map
                : StructuralTransitionKind::Unmap,
         before, capture_structural_state(state.resources(), child)});
    add_local_lifecycle_effects(result, state,
                                result.structural_transitions.back(),
                                context.input.logical_time);
    if (mapped)
      invalidate_scaled_parent_for_mapped_child(result, state, child);
  }
  return result;
}

}  // namespace glasswyrm::server::request_handlers
