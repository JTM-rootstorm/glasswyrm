#include "glasswyrmd/server_runtime.hpp"

#if GW_HAS_LIBINPUT_BACKEND

#include "protocol/x11/event.hpp"
#include "input/input_router.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace glasswyrm::server {

bool ServerRuntime::initialize_interactive_policy() {
  const auto &wire = bridge_->policy_bindings();
  if (!wire) {
    std::fprintf(stderr, "glasswyrmd: interactive policy bindings missing\n");
    return false;
  }
  glasswyrm::wm::InteractiveBindings bindings;
  bindings.move_modifiers = wire->move_modifiers;
  bindings.resize_modifiers = wire->resize_modifiers;
  bindings.close_modifiers = wire->close_modifiers;
  bindings.move_button = wire->move_button;
  bindings.resize_button = wire->resize_button;
  bindings.close_keysym = wire->close_keysym;
  bindings.minimum_width = wire->minimum_width;
  bindings.minimum_height = wire->minimum_height;
  bindings.raise_on_focus = wire->raise_on_focus != 0;
  bindings.consume_wm_bindings = wire->consume_wm_bindings != 0;
  interactive_bindings_ = bindings;
  interactive_policy_.emplace(bindings);
  std::string error;
  input::CursorGlyphSpec move;
  move.source_character = input::kCursorGlyphFleur;
  move.mask_character = input::kCursorGlyphFleur + 1U;
  move_cursor_ = input::make_glyph_cursor(move, error);
  input::CursorGlyphSpec resize;
  resize.source_character = input::kCursorGlyphBottomRightCorner;
  resize.mask_character = input::kCursorGlyphBottomRightCorner + 1U;
  resize_cursor_ = input::make_glyph_cursor(resize, error);
  if (!move_cursor_ || !resize_cursor_) {
    std::fprintf(stderr, "glasswyrmd: interactive cursor setup failed: %s\n",
                 error.c_str());
    return false;
  }
  return true;
}

bool ServerRuntime::begin_interactive_pointer(const RealInputEvent &event) {
  if (!interactive_bindings_ || !interactive_policy_ || !event.pressed)
    return false;
  const auto modifiers = static_cast<std::uint16_t>(event.state_before & 0xffU);
  glasswyrm::wm::InteractionKind kind = glasswyrm::wm::InteractionKind::None;
  if (event.detail == interactive_bindings_->move_button &&
      modifiers == interactive_bindings_->move_modifiers)
    kind = glasswyrm::wm::InteractionKind::Move;
  else if (event.detail == interactive_bindings_->resize_button &&
           modifiers == interactive_bindings_->resize_modifiers)
    kind = glasswyrm::wm::InteractionKind::ResizeBottomRight;
  if (kind == glasswyrm::wm::InteractionKind::None)
    return false;
  const auto target = glasswyrm::input::managed_top_level_ancestor(
      server_.state_.resources(), input_state_.pointer_target());
  const auto *window = server_.state_.resources().find_window(target);
  const auto found = lifecycle_->committed().windows.find(target);
  if (!window || found == lifecycle_->committed().windows.end())
    return false;
  const auto &committed = found->second;
  const auto begun = interactive_policy_->begin(
      {kind, target, event.detail, {event.root_x, event.root_y},
       {committed.applied_x, committed.applied_y, committed.applied_width,
        committed.applied_height},
       committed.managed, window->map_state == MapState::Viewable,
       window->parent == server_.state_.screen().root_window,
       window->window_class == WindowClass::InputOutput});
  if (!begun.accepted)
    return false;

  auto proposed = lifecycle_->committed();
  auto &intent = proposed.windows.at(target);
  const auto serial = server_.state_.next_lifecycle_serial();
  if (!serial) {
    abort_interactive();
    return begun.consume_event;
  }
  if (begun.focus)
    intent.focus_serial = *serial;
  if (begun.raise) {
    intent.stack_serial = *serial;
    intent.stack_sibling = 0;
    intent.stack_mode = LifecycleStackMode::Above;
  }
  LifecycleOperation operation;
  operation.token = next_lifecycle_token_++;
  operation.kind = LifecycleOperationKind::Focus;
  operation.window = target;
  operation.proposed = std::move(proposed);
  pending_real_focus_ = PendingRealFocus{server_.state_.focused_window()};
  const auto status = content_presenter_ && !bridge_->transaction_idle()
                          ? lifecycle_->enqueue_paused(std::move(operation))
                          : lifecycle_->enqueue(std::move(operation));
  if (status != EnqueueStatus::Queued) {
    pending_real_focus_.reset();
    abort_interactive();
  }
  return begun.consume_event;
}

bool ServerRuntime::update_interactive_geometry() {
  if (!interactive_policy_ ||
      interactive_policy_->kind() == glasswyrm::wm::InteractionKind::None)
    return true;
  const auto request = interactive_policy_->take_geometry_request();
  if (!request)
    return true;
  auto proposed = lifecycle_->committed();
  const auto found = proposed.windows.find(interactive_policy_->target());
  const auto serial = server_.state_.next_lifecycle_serial();
  if (found == proposed.windows.end() || !serial) {
    abort_interactive();
    return false;
  }
  found->second.requested_x = request->x;
  found->second.requested_y = request->y;
  found->second.requested_width = request->width;
  found->second.requested_height = request->height;
  found->second.geometry_serial = *serial;
  LifecycleOperation operation;
  operation.token = next_lifecycle_token_++;
  operation.kind = LifecycleOperationKind::Configure;
  operation.window = interactive_policy_->target();
  operation.proposed = std::move(proposed);
  interactive_geometry_token_ = operation.token;
  const auto status =
      content_presenter_ &&
              (content_presenter_->frame_in_flight() ||
               (cursor_presenter_ && !bridge_->transaction_idle()))
                          ? lifecycle_->enqueue_paused(std::move(operation))
                          : lifecycle_->enqueue(std::move(operation));
  if (status != EnqueueStatus::Queued) {
    interactive_geometry_token_.reset();
    abort_interactive();
    return false;
  }
  return true;
}

bool ServerRuntime::handle_interactive_close(const RealInputEvent &event) {
  if (!interactive_bindings_ || !event.pressed)
    return false;
  const auto target = server_.state_.focused_window();
  const auto *window = server_.state_.resources().find_window(target);
  const auto found = lifecycle_->committed().windows.find(target);

  const auto protocols = server_.state_.atoms().find("WM_PROTOCOLS");
  const auto delete_window = server_.state_.atoms().find("WM_DELETE_WINDOW");
  bool supports_delete = false;
  if (window && protocols && delete_window) {
    const auto property = window->properties.find(*protocols);
    if (property != window->properties.end())
      if (const auto *atoms = std::get_if<std::vector<std::uint32_t>>(
              &property->second.data))
        supports_delete = std::ranges::find(*atoms, *delete_window) != atoms->end();
  }
  const auto decision = glasswyrm::wm::evaluate_close_binding(
      *interactive_bindings_,
      static_cast<std::uint16_t>(event.state_before & 0xffU), event.keysym,
      target,
      window && found != lifecycle_->committed().windows.end() &&
          found->second.managed &&
          target != server_.state_.screen().root_window,
      window && window->attributes.override_redirect, supports_delete,
      event.time_ms);
  if (decision.action != glasswyrm::wm::CloseAction::None ||
      (event.pressed &&
       static_cast<std::uint16_t>(event.state_before & 0xffU) ==
           interactive_bindings_->close_modifiers))
    std::fprintf(stderr,
                 "glasswyrmd: close binding target=0x%08x wm_delete=%u action=%u\n",
                 target, supports_delete ? 1U : 0U,
                 static_cast<unsigned>(decision.action));
  if (decision.action == glasswyrm::wm::CloseAction::None)
    return false;
  if (decision.action == glasswyrm::wm::CloseAction::SendDeleteWindow) {
    const auto *record = server_.state_.resources().find(target);
    if (record && record->owner) {
      for (const auto &client : server_.clients_)
        if (client->identifier() == *record->owner) {
          const std::array<std::uint32_t, 5> data{*delete_window, event.time_ms,
                                                  0, 0, 0};
          (void)client->enqueue_server_packet(
              gw::protocol::x11::encode_client_message(
                  client->byte_order(), client->last_request_sequence(),
                  {target, *protocols, data, false}));
          break;
        }
    }
  } else if (decision.action == glasswyrm::wm::CloseAction::DestroyTopLevel) {
    auto proposed = server_.state_.propose_destroy_lifecycle(target);
    auto plan = server_.state_.resources().capture_destroy_plan(target);
    if (proposed && plan) {
      LifecycleOperation operation;
      operation.token = next_lifecycle_token_++;
      operation.kind = LifecycleOperationKind::Destroy;
      operation.window = target;
      operation.proposed = std::move(*proposed);
      PendingMutation mutation;
      mutation.destroy = std::move(*plan);
      pending_mutations_.emplace(operation.token, std::move(mutation));
      const auto token = operation.token;
      const auto status =
          content_presenter_ &&
                  (content_presenter_->frame_in_flight() ||
                   (cursor_presenter_ && !bridge_->transaction_idle()))
              ? lifecycle_->enqueue_paused(std::move(operation))
              : lifecycle_->enqueue(std::move(operation));
      if (status != EnqueueStatus::Queued)
        pending_mutations_.erase(token);
    }
  }
  return decision.consume_event;
}

void ServerRuntime::complete_interactive_lifecycle(
    const LifecycleOperation &operation, const bool success) {
  if (interactive_policy_ &&
      interactive_policy_->kind() != glasswyrm::wm::InteractionKind::None &&
      operation.window == interactive_policy_->target() &&
      (operation.kind == LifecycleOperationKind::Destroy ||
       operation.kind == LifecycleOperationKind::Unmap)) {
    abort_interactive();
    return;
  }
  if (!interactive_policy_ || !interactive_geometry_token_ ||
      operation.token != *interactive_geometry_token_)
    return;
  interactive_geometry_token_.reset();
  if (!success) {
    abort_interactive();
    return;
  }
  const auto found = lifecycle_->committed().windows.find(operation.window);
  if (found == lifecycle_->committed().windows.end()) {
    abort_interactive();
    return;
  }
  const auto &applied = found->second;
  (void)interactive_policy_->complete_geometry(
      {applied.applied_x, applied.applied_y, applied.applied_width,
       applied.applied_height});
  if (interactive_policy_->finish_ready())
    (void)interactive_policy_->finish();
  else
    (void)update_interactive_geometry();
}

void ServerRuntime::complete_interactive_cursor_publication() {
  if (!interactive_policy_ ||
      interactive_policy_->kind() == glasswyrm::wm::InteractionKind::None ||
      !interactive_policy_->confirm_cursor_published())
    return;
  if (interactive_policy_->finish_ready() && interactive_policy_->finish())
    mark_cursor_dirty();
}

void ServerRuntime::abort_interactive() noexcept {
  if (interactive_policy_)
    (void)interactive_policy_->abort();
  interactive_geometry_token_.reset();
  mark_cursor_dirty();
}

std::shared_ptr<const glasswyrm::input::CursorImage>
ServerRuntime::current_cursor_image() const noexcept {
  if (interactive_policy_) {
    if (interactive_policy_->cursor() ==
        glasswyrm::wm::InteractionCursor::FleurMove)
      return move_cursor_;
    if (interactive_policy_->cursor() ==
        glasswyrm::wm::InteractionCursor::BottomRightResize)
      return resize_cursor_;
  }
  if (const auto& grab = server_.state_.grabs().pointer_grab(); grab) {
    if (grab->cursor != 0)
      if (const auto* cursor =
              server_.state_.resources().find_cursor(grab->cursor))
        return cursor->image;
    if (grab->cursor_image)
      return grab->cursor_image;
  }
  const auto target = glasswyrm::input::hit_test_deepest_viewable(
      server_.state_.resources(), input_state_.pointer_x(),
      input_state_.pointer_y());
  return server_.state_.resources().effective_cursor(target);
}

} // namespace glasswyrm::server

#endif
