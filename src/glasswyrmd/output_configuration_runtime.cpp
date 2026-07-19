#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC

#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/output_configuration_events.hpp"
#include "glasswyrmd/screen_geometry.hpp"
#include "input/input_router.hpp"

#include <algorithm>
#include <cstdio>

namespace glasswyrm::server {

bool ServerRuntime::output_configuration_active() const noexcept {
  return output_configuration_stage_ != OutputConfigurationRuntimeStage::Idle ||
         (output_control_peer_ &&
          output_control_peer_->coordinator().transaction());
}

bool ServerRuntime::submit_output_policy(
    const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
    const bool rollback) {
  std::string error;
  auto* vrr = server_.options_.vrr_protocol ? bridge_->vrr_cache() : nullptr;
  if (vrr) {
    const auto* transaction = output_control_peer_
                                  ? output_control_peer_->coordinator()
                                        .transaction()
                                  : nullptr;
    if (!transaction)
      return false;
    const auto& policies = rollback ? transaction->old_vrr_policies
                                    : transaction->proposed_vrr_policies;
    if (policies.size() != layout.states.size())
      return false;
    for (const auto& [output_id, mode] : policies)
      if (!vrr->set_policy(output_id, mode))
        return false;
    synchronize_vrr_windows(snapshot, server_.state_.vrr(), *vrr);
  }
  policy_commit_ = next_policy_commit_++;
  policy_generation_ = next_policy_generation_++;
  if (!bridge_->submit_policy(
          project_policy(snapshot, policy_commit_, policy_generation_, &layout,
                         vrr),
          error)) {
    if (vrr && !rollback) {
      if (output_configuration_vrr_before_)
        *vrr = *output_configuration_vrr_before_;
    }
    std::fprintf(stderr,
                 "glasswyrmd: output configuration policy submission failed: "
                 "%s\n",
                 error.c_str());
    return false;
  }
  output_configuration_stage_ =
      rollback ? OutputConfigurationRuntimeStage::RollingBackPolicy
               : OutputConfigurationRuntimeStage::AwaitingPolicy;
  return true;
}

bool ServerRuntime::submit_output_compositor(
    const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
    const bool rollback) {
  std::string error;
  auto submission = project_compositor(
      snapshot, next_compositor_commit_++, next_compositor_generation_++,
      server_.options_.software_content, &layout,
      server_.options_.vrr_protocol ? bridge_->vrr_cache() : nullptr);
  if (submission.outputs.size() != layout.descriptors.size()) {
    std::fprintf(stderr,
                 "glasswyrmd: output configuration scene projection failed\n");
    return false;
  }
  if (content_presenter_ &&
      !content_presenter_->prepare_lifecycle(snapshot,
                                             server_.state_.resources(),
                                             submission)) {
    std::fprintf(stderr,
                 "glasswyrmd: output configuration presenter preparation "
                 "failed\n");
    return false;
  }
  if (!bridge_->submit_compositor(submission, error)) {
    if (content_presenter_)
      content_presenter_->cancel_lifecycle_submission();
    std::fprintf(stderr,
                 "glasswyrmd: output configuration compositor submission "
                 "failed: %s\n",
                 error.c_str());
    return false;
  }
  output_configuration_stage_ =
      rollback ? OutputConfigurationRuntimeStage::RollingBackCompositor
               : OutputConfigurationRuntimeStage::AwaitingCompositor;
  return true;
}

bool ServerRuntime::begin_output_rollback(
    const gw::ipc::wire::OutputConfigurationResult result,
    const bool compositor_accepted) {
  auto* transaction = output_control_peer_
                          ? output_control_peer_->coordinator().transaction()
                          : nullptr;
  if (!transaction || !output_configuration_before_ ||
      !output_control_peer_->acknowledge_compositor_rejected(result))
    return false;

  if (content_presenter_) {
    if (compositor_accepted && output_configuration_evaluated_) {
      auto scratch = server_.state_;
      content_presenter_->accept_lifecycle(*output_configuration_evaluated_,
                                           scratch.resources());
    } else {
      content_presenter_->reject_lifecycle();
    }
  }
  if (!bridge_->prepare_rollback())
    return false;
  return submit_output_policy(*output_configuration_before_,
                              transaction->old_layout, true);
}

bool ServerRuntime::promote_output_configuration() {
  auto* transaction = output_control_peer_
                          ? output_control_peer_->coordinator().transaction()
                          : nullptr;
  if (!transaction || !output_configuration_evaluated_ || !lifecycle_ ||
      lifecycle_->phase() != CoordinatorPhase::Idle || lifecycle_->active())
    return false;

  std::optional<VrrEventBatch> vrr_events;
  if (server_.options_.vrr_protocol) {
    const auto* cache = bridge_->vrr_cache();
    std::map<std::uint64_t, std::uint32_t> output_xids;
    for (const auto& output : server_.state_.randr().outputs())
      output_xids.emplace(output.internal_id, output.xid);
    if (!cache || output_xids.size() != cache->outputs().size())
      return false;
    auto batch = prepare_vrr_event_batch(*cache, server_.state_.vrr(),
                                         output_xids);
    if (batch.outputs.size() != cache->outputs().size() ||
        batch.windows.size() != cache->windows().size())
      return false;
    vrr_events = std::move(batch);
  }

  if (!output_control_peer_->coordinator().can_accept_compositor() ||
      !install_output_configuration(*output_configuration_evaluated_,
                                    transaction->proposed_layout, true))
    return false;
  if (content_presenter_)
    content_presenter_->accept_lifecycle(*output_configuration_evaluated_,
                                         server_.state_.resources());
  if (cursor_presenter_) {
    cursor_dirty_ = true;
    cursor_force_buffer_ = true;
  }
  if (!output_control_peer_->coordinator().accept_compositor()) {
    (void)install_output_configuration(*output_configuration_before_,
                                       transaction->old_layout, false);
    return false;
  }
  if (vrr_events) {
    const auto transitions = vrr_events->windows;
    apply_vrr_event_batch(server_.state_.vrr(), *vrr_events);
    for (const auto& transition : transitions) {
      const auto* after = server_.state_.vrr().find_window(transition.window_id);
      if (!after)
        continue;
      const auto changed = vrr_change_mask(transition.before, *after);
      for (const auto& client : server_.clients_) {
        const auto selection =
            after->event_selections.find(client->identifier());
        if (selection == after->event_selections.end())
          continue;
        const auto selected = changed & selection->second & kKnownVrrEventMask;
        if (selected != 0)
          (void)client->enqueue_server_packet(extensions::encode_gw_vrr_notify(
              client->byte_order(), client->last_request_sequence(), selected,
              transition.window_id, *after, transition.output_policy));
      }
    }
  }
  if (server_.protocol_event_handler_)
    server_.protocol_event_handler_(output_configuration_events_);
  output_configuration_events_.clear();
  bridge_->clear_transaction_result();
  return output_control_peer_->acknowledge_committed();
}

bool ServerRuntime::install_output_configuration(
    const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
    const bool emit_events) {
  if (!bridge_ || !lifecycle_ || !bridge_->can_adopt_output_layout(layout) ||
      !lifecycle_->can_replace_committed())
    return false;

  auto staged = server_.state_;
  const auto screen = derive_output_screen_model(layout, staged.screen());
  if (!screen || !staged.update_screen_geometry(*screen) ||
      !staged.randr().configure_output_layout(layout) ||
      !staged.commit_lifecycle(snapshot))
    return false;
  auto events = emit_events ? build_output_configuration_events(staged)
                            : std::optional<std::vector<ProtocolEventIntent>>(
                                  std::vector<ProtocolEventIntent>{});
  if (!events)
    return false;
  output_configuration_events_ = std::move(*events);

  auto staged_input = input_state_;
  const auto maximum_x =
      static_cast<std::int32_t>(layout.root_logical_width - 1U);
  const auto maximum_y =
      static_cast<std::int32_t>(layout.root_logical_height - 1U);
  const auto pointer_x = std::clamp(staged_input.pointer_x(), 0, maximum_x);
  const auto pointer_y = std::clamp(staged_input.pointer_y(), 0, maximum_y);
  staged_input.set_pointer(
      pointer_x, pointer_y,
      glasswyrm::input::hit_test_deepest_viewable(
          staged.resources(), pointer_x, pointer_y));
#if GW_HAS_LIBINPUT_BACKEND
  if (real_input_ && !real_input_->can_update_root_bounds(
                         layout.root_logical_width,
                         layout.root_logical_height))
    return false;
#endif

  const auto old_layout = *bridge_->output_layout();
  const auto old_lifecycle = lifecycle_->committed();
  auto old_state = server_.state_;
  const auto old_input = input_state_;
  const auto restore = [&] {
    output_configuration_events_.clear();
    (void)bridge_->adopt_output_layout(old_layout);
    (void)lifecycle_->replace_committed(old_lifecycle);
    server_.state_ = std::move(old_state);
    input_state_ = old_input;
#if GW_HAS_LIBINPUT_BACKEND
    if (real_input_)
      (void)real_input_->update_root_bounds(old_layout.root_logical_width,
                                            old_layout.root_logical_height);
#endif
  };

  if (!bridge_->adopt_output_layout(layout) ||
      !lifecycle_->replace_committed(snapshot)) {
    restore();
    return false;
  }
  server_.state_ = std::move(staged);
  input_state_ = staged_input;
#if GW_HAS_LIBINPUT_BACKEND
  if (real_input_ && !real_input_->update_root_bounds(
                         layout.root_logical_width,
                         layout.root_logical_height)) {
    restore();
    return false;
  }
#endif
  return true;
}

bool ServerRuntime::finish_output_configuration() {
  output_configuration_stage_ = OutputConfigurationRuntimeStage::Idle;
  output_configuration_before_.reset();
  output_configuration_evaluated_.reset();
  output_configuration_vrr_before_.reset();
  output_configuration_events_.clear();
  output_configuration_peer_reset_ = false;
  if (lifecycle_ && lifecycle_->phase() == CoordinatorPhase::Idle &&
      lifecycle_->pending_count() != 0 && !lifecycle_->resume()) {
    std::fprintf(stderr,
                 "glasswyrmd: could not resume lifecycle after output "
                 "configuration\n");
    return false;
  }
  return true;
}

bool ServerRuntime::service_output_control_work() {
  if (!output_control_peer_ || !lifecycle_)
    return true;
  auto& coordinator = output_control_peer_->coordinator();
  auto* transaction = coordinator.transaction();

  if (output_configuration_stage_ == OutputConfigurationRuntimeStage::Idle) {
    if (!transaction)
      return true;
    if (!output_control_peer_->transaction_owner_connected()) {
      if (!output_control_peer_->acknowledge_internal_error())
        return false;
      return finish_output_configuration();
    }
    if (lifecycle_->phase() != CoordinatorPhase::Idle ||
        !bridge_->transaction_idle() ||
        (content_presenter_ && content_presenter_->frame_in_flight()) ||
        (cursor_presenter_ && cursor_presenter_->in_flight()))
      return true;
    output_configuration_before_ = lifecycle_->committed();
    if (server_.options_.vrr_protocol) {
      const auto* cache = bridge_->vrr_cache();
      if (!cache)
        return false;
      output_configuration_vrr_before_ = *cache;
    }
    return submit_output_policy(*output_configuration_before_,
                                transaction->proposed_layout, false);
  }

  transaction = coordinator.transaction();
  if (!transaction || !output_configuration_before_)
    return false;

  if (output_configuration_stage_ ==
      OutputConfigurationRuntimeStage::AwaitingPolicy) {
    if (bridge_->policy_rejected_ready()) {
      bridge_->clear_transaction_result();
      if (server_.options_.vrr_protocol && output_configuration_vrr_before_)
        *bridge_->vrr_cache() = *output_configuration_vrr_before_;
      if (!output_control_peer_->acknowledge_policy_rejected())
        return false;
      return finish_output_configuration();
    }
    if (!bridge_->policy_result_ready())
      return true;
    auto evaluated = apply_policy_result(*output_configuration_before_,
                                         bridge_->policy_result(),
                                         &transaction->proposed_layout,
                                         server_.options_.vrr_protocol
                                             ? bridge_->vrr_cache()
                                             : nullptr);
    if (!evaluated || !coordinator.accept_policy())
      return false;
    output_configuration_evaluated_ = std::move(*evaluated);
    if (!output_control_peer_->transaction_owner_connected() ||
        output_configuration_peer_reset_)
      return begin_output_rollback(
          gw::ipc::wire::OutputConfigurationResult::CompositorRejected,
          false);
    if (!submit_output_compositor(*output_configuration_evaluated_,
                                  transaction->proposed_layout, false))
      return begin_output_rollback(
          gw::ipc::wire::OutputConfigurationResult::PresenterRejected,
          false);
    return true;
  }

  if (output_configuration_stage_ ==
      OutputConfigurationRuntimeStage::AwaitingCompositor) {
    if (bridge_->compositor_rejected_ready())
      return begin_output_rollback(
          gw::ipc::wire::OutputConfigurationResult::CompositorRejected,
          false);
    if (!bridge_->compositor_result_ready())
      return true;
    if (output_configuration_peer_reset_ ||
        !output_control_peer_->transaction_owner_connected())
      return begin_output_rollback(
          gw::ipc::wire::OutputConfigurationResult::CompositorRejected,
          true);
    if (!promote_output_configuration())
      return begin_output_rollback(
          gw::ipc::wire::OutputConfigurationResult::PresenterRejected,
          true);
    return finish_output_configuration();
  }

  if (output_configuration_stage_ ==
      OutputConfigurationRuntimeStage::RollingBackPolicy) {
    if (bridge_->policy_rejected_ready())
      return false;
    if (!bridge_->policy_result_ready())
      return true;
    auto rollback = apply_policy_result(*output_configuration_before_,
                                        bridge_->policy_result(),
                                        &transaction->old_layout,
                                        server_.options_.vrr_protocol
                                            ? bridge_->vrr_cache()
                                            : nullptr);
    if (!rollback ||
        !submit_output_compositor(*rollback, transaction->old_layout, true))
      return false;
    return true;
  }

  if (bridge_->compositor_rejected_ready()) {
    if (content_presenter_)
      content_presenter_->reject_lifecycle();
    (void)output_control_peer_->acknowledge_rollback(false);
    return false;
  }
  if (!bridge_->compositor_result_ready())
    return true;
  if (!install_output_configuration(*output_configuration_before_,
                                    transaction->old_layout, false))
    return false;
  if (content_presenter_)
    content_presenter_->accept_lifecycle(*output_configuration_before_,
                                         server_.state_.resources());
  bridge_->clear_transaction_result();
  if (!output_control_peer_->acknowledge_rollback(true))
    return false;
  return finish_output_configuration();
}

} // namespace glasswyrm::server

#endif
