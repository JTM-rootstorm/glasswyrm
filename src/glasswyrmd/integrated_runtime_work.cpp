#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/lifecycle_projection.hpp"

#include <cstdio>
#endif

namespace glasswyrm::server {

#ifdef GW_SERVER_HAS_IPC
bool ServerRuntime::service_peer_readiness(const short policy_events,
                                           const short compositor_events,
                                           bool& compositor_reset) {
  std::string error;
  if (!bridge_->service(policy_events, compositor_events,
                        RuntimeBridge::Clock::now(), error)) {
    std::fprintf(stderr, "glasswyrmd: integrated runtime failed: %s\n",
                 error.c_str());
    return false;
  }
  compositor_reset = bridge_->take_compositor_reset();
  return true;
}

bool ServerRuntime::service_input_session_work(
    const bool compositor_reset,
    std::vector<CompositorBufferRelease>& compositor_releases) {
#if GW_HAS_LIBINPUT_BACKEND
  if (!bridge_->ready()) {
    abort_interactive();
    (void)server_.state_.grabs().suspend();
  }
  if (!suspend_real_input_for_compositor_reset(compositor_reset) ||
      !service_session_changes() ||
      !resume_real_input_after_compositor_reset())
    return false;
#endif
  if (content_presenter_ || cursor_presenter_) {
    if (compositor_reset) {
      if (cursor_presenter_) {
        bridge_->forget_cursor_replay();
        cursor_presenter_->peer_disconnected();
#if GW_HAS_LIBINPUT_BACKEND
        cursor_submission_interactive_ = false;
        cursor_submission_diagnostic_.reset();
#endif
        cursor_dirty_ = true;
      }
      if (content_presenter_) content_presenter_->peer_disconnected();
    }
    compositor_releases = bridge_->take_buffer_releases();
  }
#if GW_HAS_LIBINPUT_BACKEND
  if (cursor_presenter_ && bridge_->cursor_rejected_ready()) {
    cursor_presenter_->reject();
    cursor_submission_interactive_ = false;
    cursor_submission_diagnostic_.reset();
    bridge_->clear_transaction_result();
    if (cursor_replay_attempted_) {
      std::fprintf(stderr, "glasswyrmd: repeated cursor frame rejection\n");
      return false;
    }
    cursor_replay_attempted_ = true;
    cursor_force_buffer_ = true;
    cursor_dirty_ = true;
  } else if (cursor_presenter_ && bridge_->cursor_result_ready()) {
    const bool interactive_cursor = cursor_submission_interactive_;
    cursor_submission_interactive_ = false;
    cursor_presenter_->accept();
    cursor_replay_attempted_ = false;
    bridge_->clear_transaction_result();
    if (cursor_submission_diagnostic_) {
      const auto& publication = *cursor_submission_diagnostic_;
      const auto kind = glasswyrm::input::cursor_kind_name(publication.kind);
      std::fprintf(stderr,
                   "glasswyrmd: cursor publication accepted kind=%.*s "
                   "x=%d y=%d visible=%u buffer=%s\n",
                   static_cast<int>(kind.size()), kind.data(), publication.x,
                   publication.y, publication.visible ? 1U : 0U,
                   publication.buffer_attached ? "attached" : "reused");
      cursor_submission_diagnostic_.reset();
    }
    if (interactive_cursor) complete_interactive_cursor_publication();
    if (lifecycle_ && lifecycle_->pending_count() != 0 &&
        !lifecycle_->resume()) {
      std::fprintf(stderr,
                   "glasswyrmd: could not resume lifecycle after cursor frame\n");
      return false;
    }
  }
#endif
  return true;
}

bool ServerRuntime::service_peer_replay(std::string& error) {
  if (!content_presenter_) return true;
  if (bridge_->content_rejected_ready()) {
    content_presenter_->reject_content();
    bridge_->clear_transaction_result();
    if (content_replay_attempted_) {
      std::fprintf(stderr,
                   "glasswyrmd: repeated compositor content rejection\n");
      return false;
    }
    content_replay_attempted_ = true;
    auto replay = project_compositor(
        lifecycle_->committed(), next_compositor_commit_++,
        next_compositor_generation_++, true);
    if (!content_presenter_->prepare_replay(
            lifecycle_->committed(), server_.state_.resources(), replay) ||
        !bridge_->submit_replay(replay, error)) {
      content_presenter_->cancel_lifecycle_submission();
      std::fprintf(stderr,
                   "glasswyrmd: compositor content replay failed: %s\n",
                   error.c_str());
      return false;
    }
  } else if (bridge_->content_result_ready()) {
    content_presenter_->accept_content();
    content_replay_attempted_ = false;
    bridge_->clear_transaction_result();
    if (lifecycle_ && lifecycle_->pending_count() != 0 &&
        !lifecycle_->resume()) {
      std::fprintf(stderr,
                   "glasswyrmd: could not resume deferred lifecycle\n");
      return false;
    }
  }
  if (bridge_->replay_rejected_ready()) {
    content_presenter_->reject_lifecycle();
    std::fprintf(stderr,
                 "glasswyrmd: compositor rejected full content replay\n");
    return false;
  }
  if (bridge_->replay_result_ready()) {
    content_presenter_->accept_lifecycle(lifecycle_->committed(),
                                         server_.state_.resources());
    bridge_->clear_transaction_result();
    content_replay_attempted_ = false;
    if (lifecycle_->pending_count() != 0 && !lifecycle_->resume()) {
      std::fprintf(stderr,
                   "glasswyrmd: could not resume lifecycle after replay\n");
      return false;
    }
  }
  return true;
}

bool ServerRuntime::service_lifecycle_work(
    const std::vector<CompositorBufferRelease>& compositor_releases) {
  if (lifecycle_ && bridge_->policy_result_ready()) {
    const auto* active = lifecycle_->active();
    auto evaluated = active
                         ? apply_policy_result(active->proposed,
                                               bridge_->policy_result())
                         : std::nullopt;
    if (!evaluated) {
      bridge_->clear_transaction_result();
      if (!lifecycle_->policy_rejected()) {
        std::fprintf(stderr,
                     "glasswyrmd: invalid policy lifecycle result\n");
        return false;
      }
    } else if (!lifecycle_->policy_accepted(std::move(*evaluated))) {
      std::fprintf(stderr,
                   "glasswyrmd: policy lifecycle transition failed\n");
      return false;
    }
  }
  if (lifecycle_ && bridge_->policy_rejected_ready() &&
      !lifecycle_->policy_rejected()) {
    std::fprintf(stderr, "glasswyrmd: policy rejection transition failed\n");
    return false;
  }
  if (lifecycle_ && bridge_->compositor_rejected_ready()) {
    if (content_presenter_) content_presenter_->reject_lifecycle();
    if (!lifecycle_->compositor_rejected()) {
      std::fprintf(stderr,
                   "glasswyrmd: compositor rollback transition failed\n");
      return false;
    }
  }
  if (lifecycle_ && bridge_->compositor_result_ready()) {
    const bool rollback =
        lifecycle_->phase() == CoordinatorPhase::RollingBackCompositor;
    if (!lifecycle_->compositor_accepted()) {
      std::fprintf(stderr,
                   "glasswyrmd: compositor lifecycle transition failed\n");
      return false;
    }
    if (rollback && content_presenter_)
      content_presenter_->accept_lifecycle(lifecycle_->committed(),
                                           server_.state_.resources());
  }
  for (const auto& release : compositor_releases) {
    const bool cursor_buffer =
        release.buffer_id >= CursorPresenter::kFirstBufferId;
    const bool released =
        cursor_buffer
            ? cursor_presenter_ &&
                  cursor_presenter_->release(release.buffer_id, release.reason)
            : content_presenter_ &&
                  content_presenter_->release(release.buffer_id, release.reason);
    if (!released) {
      std::fprintf(stderr,
                   "glasswyrmd: invalid compositor buffer release id=%llu reason=%u\n",
                   static_cast<unsigned long long>(release.buffer_id),
                   static_cast<unsigned>(release.reason));
      return false;
    }
    std::fprintf(stderr,
                 "glasswyrmd: published buffer released buffer=%llu reason=%u\n",
                 static_cast<unsigned long long>(release.buffer_id),
                 static_cast<unsigned>(release.reason));
  }
  return true;
}

bool ServerRuntime::service_output_control_work() {
  // M13 output-control work has a dedicated coordinator seam. Phase 0 keeps it
  // inert so the historical integrated reactor has no new listener or wire path.
  return true;
}

void ServerRuntime::submit_pending_content(std::string& error) {
  if (!content_presenter_ || !lifecycle_ ||
      lifecycle_->phase() != CoordinatorPhase::Idle ||
      !bridge_->transaction_idle() || content_presenter_->frame_in_flight() ||
      !content_presenter_->has_pending_damage())
    return;
  CompositorContentSubmission content;
  if (!content_presenter_->prepare_content(
          lifecycle_->committed(), server_.state_.resources(),
          next_compositor_commit_++, next_compositor_generation_++, content))
    return;
  if (!bridge_->submit_content(content, error)) {
    content_presenter_->cancel_content_submission();
    std::fprintf(stderr, "glasswyrmd: content submission failed: %s\n",
                 error.c_str());
  }
}
#endif

}  // namespace glasswyrm::server
