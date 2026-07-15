#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/lifecycle_projection.hpp"

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#endif

namespace glasswyrm::server {

int ServerRuntime::initialize_integrated(SignalRuntime& signals) {
#ifdef GW_SERVER_HAS_IPC
  server_.input_snapshot_provider_ = [this] {
    return InputSnapshot{input_state_.pointer_x(), input_state_.pointer_y(),
                         input_state_.mask(), input_state_.pointer_target(),
                         input_state_.time()};
  };
  if (!server_.options_.integrated()) return 0;

  bridge_ = std::make_unique<RuntimeBridge>(
      *server_.options_.wm_socket, *server_.options_.compositor_socket,
      server_.state_.screen(), std::chrono::seconds(10),
      server_.options_.software_content, server_.options_.real_input_enabled());
  if (server_.options_.software_content) {
    content_presenter_ = std::make_unique<ContentPresenter>();
    server_.drawable_damage_handler_ = [this](
        const std::vector<DrawableDamage>& damage) {
      for (const auto& item : damage)
        content_presenter_->damage(item.window, item.rectangle);
    };
  }
  bridge_->start();
  while (!SignalRuntime::stop_requested() && !bridge_->ready()) {
    pollfd bootstrap[3] = {
        {signals.read_descriptor(), POLLIN, 0},
        {bridge_->policy_fd(), bridge_->policy_events(), 0},
        {bridge_->compositor_fd(), bridge_->compositor_events(), 0}};
    const int count = ::poll(
        bootstrap, 3, bridge_->poll_timeout_ms(RuntimeBridge::Clock::now()));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      std::fprintf(stderr,
                   "glasswyrmd: integrated bootstrap poll failed: %s\n",
                   std::strerror(errno));
      return 1;
    }
    if ((bootstrap[0].revents & POLLIN) != 0) {
      SignalRuntime::request_stop();
      break;
    }
    std::string error;
    if (!bridge_->service(bootstrap[1].revents, bootstrap[2].revents,
                          RuntimeBridge::Clock::now(), error)) {
      std::fprintf(stderr, "glasswyrmd: integrated bootstrap failed: %s\n",
                   error.c_str());
      return 1;
    }
  }
  if (SignalRuntime::stop_requested()) return 2;

  initialize_lifecycle();
#if GW_HAS_LIBINPUT_BACKEND
  if (server_.options_.real_input_enabled() && !initialize_real_input())
    return 1;
#endif
  if (server_.options_.synthetic_input_socket) {
    input_peer_ = std::make_unique<SyntheticInputPeer>(
        *server_.options_.synthetic_input_socket);
    std::string error;
    if (!input_peer_->start(error)) {
      std::fprintf(stderr, "glasswyrmd: %s\n", error.c_str());
      return 1;
    }
  }
  return 0;
#else
  static_cast<void>(signals);
  if (server_.options_.integrated()) {
    std::fprintf(stderr,
                 "glasswyrmd: integrated mode requires libgwipc support\n");
    return 1;
  }
  return 0;
#endif
}

#ifdef GW_SERVER_HAS_IPC

bool ServerRuntime::service_integrated(const short policy_events,
                                       const short compositor_events) {
  std::string error;
  if (!bridge_->service(policy_events, compositor_events,
                        RuntimeBridge::Clock::now(), error)) {
    std::fprintf(stderr, "glasswyrmd: integrated runtime failed: %s\n",
                 error.c_str());
    return false;
  }
  const bool compositor_reset = bridge_->take_compositor_reset();
#if GW_HAS_LIBINPUT_BACKEND
  if (!suspend_real_input_for_compositor_reset(compositor_reset)) return false;
  if (!service_session_changes()) return false;
#endif
  std::vector<CompositorBufferRelease> compositor_releases;
  if (content_presenter_) {
    if (compositor_reset)
      content_presenter_->peer_disconnected();
    compositor_releases = bridge_->take_buffer_releases();
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
        content_presenter_->reject_lifecycle();
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
  }
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
  if (content_presenter_) {
    for (const auto& release : compositor_releases) {
      if (!content_presenter_->release(release.buffer_id, release.reason)) {
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
  }
  if (content_presenter_ && lifecycle_ &&
      lifecycle_->phase() == CoordinatorPhase::Idle &&
      bridge_->transaction_idle() && !content_presenter_->frame_in_flight() &&
      content_presenter_->has_pending_damage()) {
    CompositorContentSubmission content;
    if (content_presenter_->prepare_content(
            lifecycle_->committed(), server_.state_.resources(),
            next_compositor_commit_++, next_compositor_generation_++, content)) {
      if (!bridge_->submit_content(content, error)) {
        content_presenter_->reject_content();
        std::fprintf(stderr, "glasswyrmd: content submission failed: %s\n",
                     error.c_str());
      }
    }
  }
  return true;
}

#endif

}  // namespace glasswyrm::server
