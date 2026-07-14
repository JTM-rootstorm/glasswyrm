#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC

#include "glasswyrmd/lifecycle_projection.hpp"
#include "input/input_router.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <cstdio>

namespace glasswyrm::server {

void ServerRuntime::initialize_lifecycle() {
  LifecycleCallbacks callbacks;
  callbacks.send_policy =
      [this](const LifecycleSnapshot& snapshot) { return send_policy(snapshot); };
  callbacks.send_compositor = [this](const LifecycleSnapshot& snapshot) {
    return send_compositor(snapshot);
  };
  callbacks.commit = [this](const LifecycleSnapshot& snapshot) {
    return commit_lifecycle(snapshot);
  };
  callbacks.complete = [this](const std::uint64_t token, const bool success) {
    complete_lifecycle(token, success);
  };
  callbacks.fatal = [] { SignalRuntime::request_stop(); };
  callbacks.rebase = rebase_lifecycle_operation;
  callbacks.prepare_rollback = [this] { return bridge_->prepare_rollback(); };
  lifecycle_ = std::make_unique<LifecycleCoordinator>(
      server_.state_.lifecycle_snapshot(), 1024, std::move(callbacks));
  server_.deferred_lifecycle_handler_ =
      [this](ClientConnection& client, const DispatchResult& result) {
        return defer_lifecycle(client, result);
      };
  server_.cancel_lifecycle_handler_ =
      [this](const std::uint64_t client, const std::uint32_t resource_base) {
        cancel_client_lifecycle(client, resource_base);
      };
}

bool ServerRuntime::send_policy(const LifecycleSnapshot& snapshot) {
  std::string error;
  policy_commit_ = next_policy_commit_++;
  policy_generation_ = next_policy_generation_++;
  const bool sent = bridge_->submit_policy(
      project_policy(snapshot, policy_commit_, policy_generation_), error);
  if (!sent)
    std::fprintf(stderr, "glasswyrmd: policy submission failed: %s\n",
                 error.c_str());
  return sent;
}

bool ServerRuntime::send_compositor(const LifecycleSnapshot& snapshot) {
  std::string error;
  auto submission = project_compositor(
      snapshot, next_compositor_commit_++, next_compositor_generation_++,
      server_.options_.software_content);
  if (content_presenter_ && !content_presenter_->prepare_lifecycle(
                                snapshot, server_.state_.resources(), submission))
    return false;
  if (bridge_->submit_compositor(submission, error)) return true;
  if (content_presenter_) content_presenter_->reject_lifecycle();
  return false;
}

bool ServerRuntime::commit_lifecycle(const LifecycleSnapshot& snapshot) {
  transition_before_.reset();
  input_transition_before_.reset();
  const auto* active = lifecycle_ ? lifecycle_->active() : nullptr;
  if (active) {
    EventRouter router(server_.state_.resources());
    transition_before_ = router.capture(active->window);
    if (input_peer_)
      input_transition_before_ = router.capture_input_transition(
          server_.state_.focused_window(), input_state_.pointer_target());
  }
  const auto mutation = active ? pending_mutations_.find(active->token)
                               : pending_mutations_.end();
  bool committed = false;
  if (active && mutation != pending_mutations_.end() &&
      mutation->second.create) {
    const auto& pending = mutation->second;
    committed = server_.state_.commit_create_lifecycle(
        pending.owner, pending.resource_base, pending.resource_mask,
        *pending.create, pending.creation_serial, snapshot);
  } else if (active && mutation != pending_mutations_.end() &&
             mutation->second.destroy) {
    auto staged = server_.state_;
    committed = staged.resources().commit_destroy_plan(
                    *mutation->second.destroy) == DestroyWindowStatus::Success;
    committed = committed && staged.commit_lifecycle(snapshot);
    if (committed) server_.state_ = std::move(staged);
  } else if (active && mutation != pending_mutations_.end() &&
             mutation->second.cleanup) {
    auto staged = server_.state_;
    (void)staged.resources().commit_client_cleanup(*mutation->second.cleanup);
    committed = staged.commit_lifecycle(snapshot);
    if (committed) server_.state_ = std::move(staged);
  } else {
    committed = server_.state_.commit_lifecycle(snapshot);
  }
  if (!committed) {
    if (content_presenter_) content_presenter_->reject_lifecycle();
    return false;
  }
  if (content_presenter_)
    content_presenter_->accept_lifecycle(snapshot, server_.state_.resources());
  return true;
}

void ServerRuntime::complete_lifecycle(const std::uint64_t token,
                                       const bool success) {
  ClientConnection* requester = nullptr;
  for (const auto& client : server_.clients_)
    if (client->clear_dispatch_blocked(token)) requester = client.get();
  const auto* operation = lifecycle_ ? lifecycle_->active() : nullptr;
  const auto mutation = pending_mutations_.find(token);
  const auto cleanup_base =
      mutation != pending_mutations_.end() && mutation->second.cleanup
          ? std::optional<std::uint32_t>(mutation->second.resource_base)
          : std::nullopt;
  if (success && operation) {
    std::vector<ClientConnection*> recipients;
    recipients.reserve(server_.clients_.size());
    for (const auto& client : server_.clients_)
      recipients.push_back(client.get());
    EventRouter router(server_.state_.resources());
    if (operation->kind == LifecycleOperationKind::Destroy &&
        mutation != pending_mutations_.end() && mutation->second.destroy) {
      for (const auto& item : mutation->second.destroy->postorder) {
        StructuralEventState before{};
        before.target = item.xid;
        before.parent = item.parent;
        before.structure_recipients = item.structure_recipients;
        before.substructure_recipients = item.substructure_recipients;
        (void)router.route_transition(StructuralTransitionKind::Destroy, before,
                                      std::nullopt, recipients);
      }
    } else if (operation->kind == LifecycleOperationKind::ClientCleanup &&
               mutation != pending_mutations_.end() &&
               mutation->second.cleanup) {
      for (const auto& item : mutation->second.cleanup->postorder) {
        StructuralEventState before{};
        before.target = item.xid;
        before.parent = item.parent;
        before.structure_recipients = item.structure_recipients;
        before.substructure_recipients = item.substructure_recipients;
        (void)router.route_transition(StructuralTransitionKind::Destroy, before,
                                      std::nullopt, recipients);
      }
    } else {
      const auto committed = router.capture(operation->window);
      if (operation->kind != LifecycleOperationKind::Create) {
        const auto kind = operation->kind == LifecycleOperationKind::Map
                              ? StructuralTransitionKind::Map
                          : operation->kind == LifecycleOperationKind::Unmap
                              ? StructuralTransitionKind::Unmap
                          : operation->kind == LifecycleOperationKind::Destroy
                              ? StructuralTransitionKind::Destroy
                              : StructuralTransitionKind::Configure;
        (void)router.route_transition(kind, transition_before_, committed,
                                      recipients);
        if (committed && operation->kind == LifecycleOperationKind::Map &&
            (!transition_before_ || !transition_before_->viewable) &&
            committed->viewable) {
          (void)router.route_viewable_subtree_expose(operation->window,
                                                     recipients);
        } else if (committed && transition_before_ &&
                   operation->kind == LifecycleOperationKind::Configure) {
          std::vector<geometry::Rectangle> rectangles;
          if (committed->width > transition_before_->width)
            rectangles.push_back(
                {static_cast<std::int32_t>(transition_before_->width), 0,
                 static_cast<std::uint32_t>(committed->width -
                                            transition_before_->width),
                 committed->height});
          if (committed->height > transition_before_->height)
            rectangles.push_back(
                {0, static_cast<std::int32_t>(transition_before_->height),
                 std::min<std::uint16_t>(transition_before_->width,
                                         committed->width),
                 static_cast<std::uint32_t>(committed->height -
                                            transition_before_->height)});
          (void)router.route_expose(operation->window, rectangles, recipients);
        }
      }
    }
    if (input_peer_ && input_transition_before_ &&
        operation->kind != LifecycleOperationKind::Focus) {
      const auto new_target = glasswyrm::input::hit_test_top_level(
          server_.state_.resources(), input_state_.pointer_x(),
          input_state_.pointer_y());
      input_state_.set_pointer(input_state_.pointer_x(), input_state_.pointer_y(),
                               new_target);
      (void)router.route_lifecycle_input_transition(
          *input_transition_before_, server_.state_.focused_window(), new_target,
          input_state_, recipients);
    }
  }
  if (operation && operation->kind == LifecycleOperationKind::Focus &&
      pending_focus_input_) {
    const auto new_focus = server_.state_.focused_window();
    if (success) {
      std::vector<ClientConnection*> recipients;
      recipients.reserve(server_.clients_.size());
      for (const auto& client : server_.clients_)
        recipients.push_back(client.get());
      EventRouter router(server_.state_.resources());
      pending_focus_input_->delivered += router.route_focus(
          pending_focus_input_->old_focus, new_focus, recipients);
    }
    if (input_peer_ && input_peer_->connected() &&
        pending_focus_input_->provider_connected) {
      gwipc_synthetic_input_acknowledged acknowledgement{};
      acknowledgement.struct_size = sizeof(acknowledgement);
      acknowledgement.input_id = pending_focus_input_->record.input_id;
      acknowledgement.time_ms = input_state_.time();
      acknowledgement.result = success ? GWIPC_SYNTHETIC_INPUT_ACCEPTED
                                       : GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED;
      acknowledgement.root_x = input_state_.pointer_x();
      acknowledgement.root_y = input_state_.pointer_y();
      acknowledgement.pointer_window = input_state_.pointer_target();
      acknowledgement.focus_window = new_focus;
      acknowledgement.state = input_state_.mask();
      acknowledgement.delivered_event_count =
          static_cast<std::uint32_t>(pending_focus_input_->delivered);
      if (!input_peer_->acknowledge(pending_focus_input_->record,
                                    acknowledgement))
        input_peer_->disconnect();
    }
    pending_focus_input_.reset();
  }
  (void)requester;
  transition_before_.reset();
  input_transition_before_.reset();
  pending_mutations_.erase(token);
  if (cleanup_base) server_.pending_resource_bases_.erase(*cleanup_base);
  bridge_->clear_transaction_result();
}

bool ServerRuntime::defer_lifecycle(ClientConnection& client,
                                    const DispatchResult& result) {
  auto proposed = lifecycle_->committed();
  const auto serial = server_.state_.next_lifecycle_serial();
  if (!serial) return false;
  LifecycleOperation operation;
  operation.token = next_lifecycle_token_++;
  operation.client_id = client.identifier();
  operation.request_sequence = client.last_request_sequence();
  operation.window = result.deferred_window;
  PendingMutation mutation;
  if (result.deferred_create) {
    operation.kind = LifecycleOperationKind::Create;
    auto create = server_.state_.propose_create_lifecycle(
        client.identifier(), client.resource_id_base(),
        server_.state_.screen().resource_id_mask, *result.deferred_create,
        *serial);
    if (!create) return false;
    proposed = std::move(*create);
    mutation.create = result.deferred_create;
    mutation.owner = client.identifier();
    mutation.resource_base = client.resource_id_base();
    mutation.resource_mask = server_.state_.screen().resource_id_mask;
    mutation.creation_serial = *serial;
  } else if (result.deferred_destroy) {
    operation.kind = LifecycleOperationKind::Destroy;
    auto destroy =
        server_.state_.propose_destroy_lifecycle(result.deferred_window);
    auto plan =
        server_.state_.resources().capture_destroy_plan(result.deferred_window);
    if (!destroy || !plan) return false;
    proposed = std::move(*destroy);
    mutation.destroy = std::move(*plan);
  } else {
    auto found = proposed.windows.find(result.deferred_window);
    if (found == proposed.windows.end()) return false;
    if (result.deferred_override_redirect) {
      operation.kind = LifecycleOperationKind::OverrideChange;
      found->second.override_redirect = *result.deferred_override_redirect;
    } else if (result.deferred_configure) {
      operation.kind = LifecycleOperationKind::Configure;
      const auto& request = *result.deferred_configure;
      if (request.x) found->second.requested_x = *request.x;
      if (request.y) found->second.requested_y = *request.y;
      if (request.width) found->second.requested_width = *request.width;
      if (request.height) found->second.requested_height = *request.height;
      if (request.border_width)
        found->second.requested_border_width = *request.border_width;
      found->second.geometry_serial = *serial;
      found->second.stack_sibling = request.sibling.value_or(0);
      found->second.stack_mode =
          request.stack_mode == gw::protocol::x11::CoreStackMode::Above
              ? LifecycleStackMode::Above
          : request.stack_mode == gw::protocol::x11::CoreStackMode::Below
              ? LifecycleStackMode::Below
              : LifecycleStackMode::None;
      if (request.stack_mode) found->second.stack_serial = *serial;
    } else {
      operation.kind = result.deferred_map ? LifecycleOperationKind::Map
                                           : LifecycleOperationKind::Unmap;
      found->second.map_requested = result.deferred_map;
      found->second.map_serial = *serial;
    }
  }
  operation.proposed = std::move(proposed);
  const auto token = operation.token;
  pending_mutations_.emplace(token, std::move(mutation));
  const auto status = content_presenter_ && content_presenter_->frame_in_flight()
                          ? lifecycle_->enqueue_paused(std::move(operation))
                          : lifecycle_->enqueue(std::move(operation));
  if (status == EnqueueStatus::Queued) {
    client.set_dispatch_blocked(token);
    return true;
  }
  pending_mutations_.erase(token);
  if (status == EnqueueStatus::Full) {
    return client.enqueue_server_packet(gw::protocol::x11::encode_core_error(
        client.byte_order(),
        {gw::protocol::x11::CoreErrorCode::BadAlloc,
         client.last_request_sequence(), 0,
         static_cast<std::uint8_t>(
             result.deferred_configure
                 ? gw::protocol::x11::CoreOpcode::ConfigureWindow
             : result.deferred_override_redirect
                 ? gw::protocol::x11::CoreOpcode::ChangeWindowAttributes
             : result.deferred_map ? gw::protocol::x11::CoreOpcode::MapWindow
                                   : gw::protocol::x11::CoreOpcode::UnmapWindow),
         0}));
  }
  std::fprintf(stderr,
               "glasswyrmd: lifecycle enqueue failed token=%llu status=%u phase=%u\n",
               static_cast<unsigned long long>(token),
               static_cast<unsigned>(status),
               static_cast<unsigned>(lifecycle_->phase()));
  return false;
}

void ServerRuntime::cancel_client_lifecycle(
    const std::uint64_t client, const std::uint32_t resource_base) {
  lifecycle_->cancel_client(client);
  auto plan = server_.state_.resources().prepare_client_cleanup(client);
  if (!plan.affects_policy) {
    (void)server_.state_.resources().commit_client_cleanup(plan);
    server_.pending_resource_bases_.erase(resource_base);
    return;
  }
  auto proposed = lifecycle_->committed();
  for (const auto& item : plan.postorder) {
    proposed.windows.erase(item.xid);
    std::erase(proposed.root_order, item.xid);
    if (proposed.focused_window == item.xid)
      proposed.focused_window = proposed.root_window;
  }
  LifecycleOperation operation;
  operation.token = next_lifecycle_token_++;
  operation.client_id = client;
  operation.kind = LifecycleOperationKind::ClientCleanup;
  operation.window = plan.roots.empty() ? 0 : plan.roots.front();
  operation.proposed = std::move(proposed);
  const auto token = operation.token;
  PendingMutation mutation;
  mutation.resource_base = resource_base;
  mutation.cleanup = std::move(plan);
  pending_mutations_.emplace(token, std::move(mutation));
  const auto cleanup_status =
      content_presenter_ && content_presenter_->frame_in_flight()
          ? lifecycle_->enqueue_priority_paused(std::move(operation))
          : lifecycle_->enqueue_priority(std::move(operation));
  if (cleanup_status != EnqueueStatus::Queued) {
    std::fprintf(stderr,
                 "glasswyrmd: could not queue coordinated client cleanup\n");
    SignalRuntime::request_stop();
  }
}

}  // namespace glasswyrm::server

#endif
